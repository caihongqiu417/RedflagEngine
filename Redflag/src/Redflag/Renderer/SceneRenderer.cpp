#include "rfpch.h"
#include "SceneRenderer.h"

#include "Redflag/Scene/Scene.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Scene/Components.h"

#include "Redflag/Renderer/Renderer2D.h"
#include "Redflag/Renderer/Renderer3D.h"
#include "Redflag/Renderer/IBLPrecompute.h"
#include "Redflag/Renderer/Framebuffer.h"
#include "Redflag/ParticleSystem/ParticleSystem.h"
#include "Redflag/Renderer/SubTexture2D.h"
#include "Redflag/Renderer/Shader.h"

#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Renderer/RHI/CommandList.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <random>

namespace Redflag {

	// ---- 灯光资源 ----
	static Ref<UniformBuffer> s_LightUBO;

	// ---- 阴影资源 ----
	// 单张 2048×2048 深度图，使用主绘制的第一盏有效方向光（须允许投影），正交半宽由面板设置。
	static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
	static constexpr uint32_t SHADOW_MAP_SLOT = 11;

	static Ref<Framebuffer> s_ShadowFBO;
	static Ref<Shader>      s_ShadowDepthShader;
	static Ref<Shader>      s_DepthPrepassShader;

	static ShadowSettings s_ShadowSettings;

	static bool s_IBLEnabled       = true;
	static bool s_ShadowEnabled    = true;
	static bool s_ZPrepassEnabled  = true;
	static bool s_SSAOEnabled      = true;

	ShadowSettings& SceneRenderer::GetShadowSettings()  { return s_ShadowSettings; }
	bool& SceneRenderer::GetIBLEnabled()                 { return s_IBLEnabled; }
	bool& SceneRenderer::GetShadowEnabled()              { return s_ShadowEnabled; }
	bool& SceneRenderer::GetZPrepassEnabled()            { return s_ZPrepassEnabled; }
	bool& SceneRenderer::GetSSAOEnabled()                { return s_SSAOEnabled; }

	// ---- SSAO 资源 ----
	// 用主相机的深度和法线估算环境光遮蔽。主画面用这张图压暗墙角等受遮挡的地方。
	static constexpr uint32_t SSAO_KERNEL_SIZE = 64;
	static constexpr uint32_t SSAO_UBO_BINDING  = 4;
	static constexpr uint32_t SSAO_MAP_SLOT     = 12;
	static constexpr uint32_t SSAO_DEPTH_SLOT   = 13;
	static constexpr uint32_t SSAO_NORMAL_SLOT  = 14;
	static constexpr uint32_t SSAO_NOISE_SLOT   = 15;

	struct GPUSSAOParams
	{
		glm::mat4 Projection;
		glm::mat4 InvProjection;
		glm::vec4 Params;      // 依次是采样半径、判断遮挡所需的深度差、遮蔽强度的指数、采样点数。
		glm::vec4 NoiseScale;  // xy 控制 4×4 随机方向图在屏幕上重复多少次
		glm::vec4 Kernel[SSAO_KERNEL_SIZE];
	};

	static SSAOSettings      s_SSAOSettings;
	static Ref<Framebuffer>  s_SSAOFBO;
	static Ref<Shader>       s_SSAOShader;
	static Ref<UniformBuffer> s_SSAOParamsUBO;
	static uint32_t          s_SSAONoiseTex = 0;
	static uint32_t          s_SSAOWhiteTex = 0;
	static uint32_t          s_SSAOQuadVAO  = 0;
	static uint32_t          s_SSAOQuadVBO  = 0;
	static glm::vec4         s_SSAOKernel[SSAO_KERNEL_SIZE]{};
	// 深度预写留下的法线图，供面板预览。
	static uint32_t          s_LastViewNormalTex    = 0;
	static uint32_t          s_LastPrepassDepthTex  = 0;

	// 深度预览使用本帧物体的深度范围，灰度差别会更明显。
	static uint32_t          s_DepthVizTex      = 0;
	static uint32_t          s_DepthVizTexW     = 0;
	static uint32_t          s_DepthVizTexH     = 0;

	SSAOSettings& SceneRenderer::GetSSAOSettings() { return s_SSAOSettings; }
	uint32_t SceneRenderer::GetSSAOTextureID()
	{
		return s_SSAOFBO ? s_SSAOFBO->GetColorAttachmentRendererID(0) : 0;
	}
	uint32_t SceneRenderer::GetViewNormalTextureID()
	{
		return s_LastViewNormalTex;
	}
	uint32_t SceneRenderer::GetShadowMapTextureID()
	{
		return s_ShadowFBO ? s_ShadowFBO->GetDepthAttachmentRendererID() : 0;
	}
	uint32_t SceneRenderer::GetPrepassDepthTextureID()
	{
		return s_LastPrepassDepthTex;
	}

	// ---- 后处理资源 ----
	// PostProcess 拥有输出，主场景颜色 ID 只借用到本帧后处理结束。
	static Scope<PostProcess> s_PostProcess;
	static uint32_t s_MainSceneColorTex = 0;
	static int s_MainSceneWidth = 0;
	static int s_MainSceneHeight = 0;

	BloomSettings& SceneRenderer::GetBloomSettings() { return s_PostProcess->GetBloomSettings(); }
	ToneMapSettings& SceneRenderer::GetToneMapSettings() { return s_PostProcess->GetToneMapSettings(); }

	// ---- RSM 光源视角资源 ----
	// 用 512×512 图像记录受光表面的反射通量密度、位置与朝向，供间接光计算读取。
	static constexpr uint32_t RSM_MAP_SIZE        = 512;
	static constexpr uint32_t RSM_FLUX_SLOT       = 13;
	static constexpr uint32_t RSM_WORLDPOS_SLOT   = 14;
	static constexpr uint32_t RSM_NORMAL_SLOT     = 15;
	static constexpr uint32_t RSM_DEPTH_SLOT      = 16;
	static constexpr uint32_t RSM_UBO_BINDING     = 6;
	static constexpr uint32_t RSM_MAX_SAMPLES     = 128;

	// 提供给间接光计算的采样参数，字段顺序与 RSMIndirect.glsl 对应。
	// Header：点数、光照倍率、相对光源图宽度的半径、光源投影边长（世界单位）。
	// Samples：xy 是单位圆盘内的位置，z 是权重，w 不使用。
	struct GPURSMParams
	{
		glm::vec4 Header;
		glm::vec4 Samples[RSM_MAX_SAMPLES];
	};

	static RSMSettings        s_RSMSettings;
	static Ref<Framebuffer>   s_RSMFBO;
	static Ref<Shader>        s_RSMDepthShader;
	static Ref<UniformBuffer> s_RSMParamsUBO;

	// 关闭 RSM 时让主绘制读取黑图，间接光贡献为零。
	static uint32_t           s_RSMBlackTex = 0;

	// ---- 合并前后两帧的间接光 ----
	static constexpr uint32_t RSM_INDIRECT_SCENE_DEPTH_SLOT = 18;
	static constexpr uint32_t RSM_INDIRECT_VIEW_NORMAL_SLOT = 19;
	static constexpr uint32_t RSM_RESOLVED_SLOT             = 17;

	static Ref<Framebuffer>   s_RSMIndirectFBO;       // 本帧单独算出的间接光，与屏幕同尺寸
	static Ref<Framebuffer>   s_RSMHistoryFBO[2];     // 两张图交替读写，保留上次混合结果
	static int                s_RSMHistoryIndex = 0;  // 下次把混好的间接光写到哪张图（0 或 1）
	static uint32_t           s_FrameIndex      = 0;  // 每帧转动采样点，避免总取到相同位置
	static glm::mat4          s_PrevViewProj    = glm::mat4(1.0f);
	static bool               s_RSMHistoryValid = false; // 上一帧的间接光还能否用于这次绘制
	static float              s_RSMBlendAlpha   = 0.9f;  // 默认混入九成上一帧的间接光。调低后，本帧新算出的反射光占比更大。
	// 记录上次使用的场景、相机与光照，避免把另一幅画面的颜色混进来。
	static const Scene*       s_RSMScene = nullptr;
	static const EditorCamera* s_RSMEditorCamera = nullptr; // nullptr 表示运行时入口
	static glm::mat4          s_RSMProjection{ 1.0f };
	static glm::vec4          s_RSMLightDirection{}, s_RSMLightColor{};
	static GPURSMParams       s_RSMParameters{};

	static Ref<Shader>        s_RSMIndirectShader;
	static Ref<Shader>        s_RSMTemporalResolveShader;

	static constexpr uint32_t RSM_INDIRECT_PARAMS_BINDING  = 7;
	static constexpr uint32_t RSM_TEMPORAL_PARAMS_BINDING  = 8;

	struct GPURSMIndirectParams
	{
		glm::mat4 InvViewProj;
		int       FrameIndex;
		int       _pad0, _pad1, _pad2;
	};

	struct GPURSMTemporalParams
	{
		glm::mat4 InvViewProj;
		glm::mat4 PrevViewProj;
		float     BlendAlpha;
		int       ResetHistory;
		float     _pad0, _pad1;
	};

	static Ref<UniformBuffer> s_RSMIndirectParamsUBO;
	static Ref<UniformBuffer> s_RSMTemporalParamsUBO;

	RSMSettings& SceneRenderer::GetRSMSettings() { return s_RSMSettings; }
	float& SceneRenderer::GetRSMBlendAlpha()      { return s_RSMBlendAlpha; }

	uint32_t SceneRenderer::GetRSMFluxTextureID()
	{
		return s_RSMFBO ? s_RSMFBO->GetColorAttachmentRendererID(0) : 0;
	}
	uint32_t SceneRenderer::GetRSMNormalTextureID()
	{
		return s_RSMFBO ? s_RSMFBO->GetColorAttachmentRendererID(2) : 0;
	}
	uint32_t SceneRenderer::GetRSMDistanceTextureID()
	{
		// 光源图的深度值，表示近远裁剪面之间的位置，不是世界单位的距离。
		return s_RSMFBO ? s_RSMFBO->GetDepthAttachmentRendererID() : 0;
	}
	uint32_t SceneRenderer::GetRSMResolvedTextureID()
	{
		// 索引已经指向下次要写的图，面板读取刚画完的另一张。
		int readIdx = 1 - s_RSMHistoryIndex;
		return (s_RSMHistoryValid && s_RSMHistoryFBO[readIdx])
			? s_RSMHistoryFBO[readIdx]->GetColorAttachmentRendererID(0) : 0;
	}
	uint32_t SceneRenderer::GetBloomTextureID()
	{
		return s_PostProcess->GetBloomTextureID();
	}
	void SceneRenderer::SetMainSceneColorTarget(uint32_t colorTex, int width, int height)
	{
		s_MainSceneColorTex = colorTex;
		s_MainSceneWidth = width;
		s_MainSceneHeight = height;
	}
	uint32_t SceneRenderer::GetFinalColorTextureID()
	{
		return s_PostProcess->GetColorTextureID();
	}

	// 灯光数量存在开头，后面为两类灯各自留出固定空间。
	static constexpr uint32_t LIGHT_UBO_SIZE =
		sizeof(GPULightUBO) +
		(MAX_DIRECTIONAL_LIGHTS + MAX_POINT_LIGHTS) * sizeof(GPULightData);

	// ---- 初始化与销毁 ----
	void SceneRenderer::Init()
	{
		s_LightUBO = UniformBuffer::Create(LIGHT_UBO_SIZE, LIGHT_UBO_BINDING);

		// ---- 阴影图 ----
		// 只保存离光源最近的表面深度。
		FramebufferSpecification shadowSpec;
		shadowSpec.Width  = SHADOW_MAP_SIZE;
		shadowSpec.Height = SHADOW_MAP_SIZE;
		shadowSpec.Attachments = { FramebufferTextureFormat::DEPTH32F };
		s_ShadowFBO = Framebuffer::Create(shadowSpec);

		s_ShadowDepthShader  = Shader::Create("assets/shaders/ShadowDepth.glsl");
		s_DepthPrepassShader = Shader::Create("assets/shaders/Depth_Prepass.glsl");

		// ---- SSAO 资源 ----
		// 遮蔽图随视口缩放。主画面从红色通道读取环境光能照到这里的比例。
		FramebufferSpecification ssaoSpec;
		ssaoSpec.Width  = 1280;
		ssaoSpec.Height = 720;
		ssaoSpec.Attachments = { FramebufferTextureFormat::RGBA8 };
		s_SSAOFBO = Framebuffer::Create(ssaoSpec);

		s_SSAOShader    = Shader::Create("assets/shaders/SSAO.glsl");
		s_SSAOParamsUBO = UniformBuffer::Create(sizeof(GPUSSAOParams), SSAO_UBO_BINDING);

		// 在表面法线朝向的半球内取 64 个点，检查近处是否有几何体挡光。
		{
			std::default_random_engine gen(1337u);
			std::uniform_real_distribution<float> rnd01(0.0f, 1.0f);
			for (uint32_t i = 0; i < SSAO_KERNEL_SIZE; ++i)
			{
				glm::vec3 s(rnd01(gen) * 2.0f - 1.0f,
							rnd01(gen) * 2.0f - 1.0f,
							rnd01(gen));
				s = glm::normalize(s) * rnd01(gen);
				float scale = float(i) / float(SSAO_KERNEL_SIZE);
				// 靠近表面多取些点，墙角和接触处的小遮挡更容易被采到。
				scale = 0.1f + (1.0f - 0.1f) * scale * scale;
				s *= scale;
				s_SSAOKernel[i] = glm::vec4(s, 0.0f);
			}
		}

		// 平铺这张 4×4 的随机方向图，让相邻像素沿不同方向寻找遮挡。
		{
			std::default_random_engine gen(42u);
			std::uniform_real_distribution<float> rnd01(0.0f, 1.0f);
			glm::vec4 noise[16];
			for (int i = 0; i < 16; ++i)
				noise[i] = glm::vec4(rnd01(gen) * 2.0f - 1.0f,
									  rnd01(gen) * 2.0f - 1.0f,
									  0.0f, 0.0f);
			glCreateTextures(GL_TEXTURE_2D, 1, &s_SSAONoiseTex);
			glTextureStorage2D(s_SSAONoiseTex, 1, GL_RGBA16F, 4, 4);
			glTextureSubImage2D(s_SSAONoiseTex, 0, 0, 0, 4, 4, GL_RGBA, GL_FLOAT, noise);
			glTextureParameteri(s_SSAONoiseTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTextureParameteri(s_SSAONoiseTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTextureParameteri(s_SSAONoiseTex, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTextureParameteri(s_SSAONoiseTex, GL_TEXTURE_WRAP_T, GL_REPEAT);
		}

		// 白色表示环境光完全可见，关闭 SSAO 时主画面就用这张图。
		{
			glCreateTextures(GL_TEXTURE_2D, 1, &s_SSAOWhiteTex);
			glTextureStorage2D(s_SSAOWhiteTex, 1, GL_RGBA8, 1, 1);
			uint32_t white = 0xFFFFFFFFu;
			glTextureSubImage2D(s_SSAOWhiteTex, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &white);
			glTextureParameteri(s_SSAOWhiteTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTextureParameteri(s_SSAOWhiteTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}

		// 三角形的两条边伸出视口，一次绘制就能覆盖整幅画面。
		{
			float verts[] = {
				-1.0f, -1.0f,
				 3.0f, -1.0f,
				-1.0f,  3.0f,
			};
			glCreateVertexArrays(1, &s_SSAOQuadVAO);
			glCreateBuffers(1, &s_SSAOQuadVBO);
			glNamedBufferData(s_SSAOQuadVBO, sizeof(verts), verts, GL_STATIC_DRAW);
			glVertexArrayVertexBuffer(s_SSAOQuadVAO, 0, s_SSAOQuadVBO, 0, sizeof(float) * 2);
			glEnableVertexArrayAttrib(s_SSAOQuadVAO, 0);
			glVertexArrayAttribFormat(s_SSAOQuadVAO, 0, 2, GL_FLOAT, GL_FALSE, 0);
			glVertexArrayAttribBinding(s_SSAOQuadVAO, 0, 0);
		}

		s_PostProcess = CreateScope<PostProcess>();

		// ---- 反射阴影图 ----
		// 物体边缘两侧的像素可能来自不同表面。读取反射点时取最近的像素，避免把两个物体的位置或朝向混在一起。
		{
			FramebufferSpecification rsmSpec;
			rsmSpec.Width  = RSM_MAP_SIZE;
			rsmSpec.Height = RSM_MAP_SIZE;
			rsmSpec.Attachments = {
				FramebufferTextureFormat::RGBA16F,  // 向外反射的漫射光
				FramebufferTextureFormat::RGBA16F,  // 世界位置
				FramebufferTextureFormat::RGBA16F,  // 世界空间法线
				FramebufferTextureFormat::DEPTH32F  // 保留光源能看到的最近表面
			};
			s_RSMFBO = Framebuffer::Create(rsmSpec);

			if (s_RSMFBO)
			{
				for (uint32_t i = 0; i < 3; ++i)
				{
					uint32_t tex = s_RSMFBO->GetColorAttachmentRendererID(i);
					glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
					glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				}
			}
		}

		s_RSMDepthShader             = Shader::Create("assets/shaders/RSMDepth.glsl");
		s_RSMIndirectShader          = Shader::Create("assets/shaders/RSMIndirect.glsl");
		s_RSMTemporalResolveShader   = Shader::Create("assets/shaders/RSMTemporalResolve.glsl");
		s_RSMParamsUBO               = UniformBuffer::Create(sizeof(GPURSMParams), RSM_UBO_BINDING);
		s_RSMIndirectParamsUBO       = UniformBuffer::Create(sizeof(GPURSMIndirectParams), RSM_INDIRECT_PARAMS_BINDING);
		s_RSMTemporalParamsUBO       = UniformBuffer::Create(sizeof(GPURSMTemporalParams), RSM_TEMPORAL_PARAMS_BINDING);

		// 关闭间接光时供主绘制读取的黑图。
		{
			glCreateTextures(GL_TEXTURE_2D, 1, &s_RSMBlackTex);
			glTextureStorage2D(s_RSMBlackTex, 1, GL_RGBA16F, 1, 1);
			float black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			glTextureSubImage2D(s_RSMBlackTex, 0, 0, 0, 1, 1, GL_RGBA, GL_FLOAT, black);
			glTextureParameteri(s_RSMBlackTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTextureParameteri(s_RSMBlackTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}
	}

	void SceneRenderer::Shutdown()
	{
		// 先释放此处持有的 GPU 对象，调用方随后才能销毁窗口和图形上下文。
		s_LightUBO.reset();
		s_ShadowFBO.reset();
		s_ShadowDepthShader.reset();
		s_DepthPrepassShader.reset();
		s_SSAOFBO.reset();
		s_SSAOShader.reset();
		s_SSAOParamsUBO.reset();
		if (s_DepthVizTex) { glDeleteTextures(1, &s_DepthVizTex); s_DepthVizTex = 0; }
		if (s_SSAONoiseTex) { glDeleteTextures(1, &s_SSAONoiseTex); s_SSAONoiseTex = 0; }
		if (s_SSAOWhiteTex) { glDeleteTextures(1, &s_SSAOWhiteTex); s_SSAOWhiteTex = 0; }
		if (s_SSAOQuadVBO)  { glDeleteBuffers (1, &s_SSAOQuadVBO);  s_SSAOQuadVBO  = 0; }
		if (s_SSAOQuadVAO)  { glDeleteVertexArrays(1, &s_SSAOQuadVAO); s_SSAOQuadVAO = 0; }
		s_PostProcess.reset();
		s_LastViewNormalTex = 0;
		s_LastPrepassDepthTex = 0;
		s_MainSceneColorTex = 0;
		s_MainSceneWidth = 0;
		s_MainSceneHeight = 0;
		// RSM 资源
		s_RSMFBO.reset();
		s_RSMDepthShader.reset();
		s_RSMIndirectShader.reset();
		s_RSMTemporalResolveShader.reset();
		s_RSMParamsUBO.reset();
		s_RSMIndirectParamsUBO.reset();
		s_RSMTemporalParamsUBO.reset();
		s_RSMIndirectFBO.reset();
		s_RSMHistoryFBO[0].reset();
		s_RSMHistoryFBO[1].reset();
		s_RSMHistoryValid = false;
		s_RSMParameters = {};
		s_RSMScene = nullptr;
		s_RSMEditorCamera = nullptr;
		s_FrameIndex      = 0;
		if (s_RSMBlackTex) { glDeleteTextures(1, &s_RSMBlackTex); s_RSMBlackTex = 0; }
		IBLPrecompute::Shutdown();
	}

	// ---- 全屏绘制 ----
	static void DrawFullscreenTriangle()
	{
		glBindVertexArray(s_SSAOQuadVAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
	}

	static void UpdateRSMView(const Scene& scene, const glm::mat4& projection,
	                          const EditorCamera* editorCamera = nullptr)
	{
		if (s_RSMScene != &scene || s_RSMEditorCamera != editorCamera || s_RSMProjection != projection)
			s_RSMHistoryValid = false;
		s_RSMScene = &scene;
		s_RSMEditorCamera = editorCamera;
		s_RSMProjection = projection;
		// 移动相机时，着色器会找回墙面在上一帧的位置。游戏运行时没有传入相机对象，换成投影相同的另一台相机也会沿用上一帧的间接光。
	}

	// ---- 光源矩阵 ----
	// 用主绘制的第一盏有效方向光生成投影，不能为阴影另选一盏灯，否则明暗方向会不一致。
	// 输出把世界位置映射到光源画面的矩阵。覆盖范围围绕原点，深度范围为 -50 到 50。
	static bool TryBuildLightSpaceMatrix(Scene& scene, glm::mat4& outMatrix)
	{
		entt::registry& registry = scene.GetRegistry();

		bool found = false;
		glm::vec3 dir(0.0f);

		auto view = registry.view<TransformComponent, DirectionalLightComponent>();
		for (auto e : view)
		{
			auto& tc = view.get<TransformComponent>(e);
			auto& dl = view.get<DirectionalLightComponent>(e);
			dir = dl.WorldDirection(tc);
			if (glm::dot(dir, dir) < 1e-6f) continue;
			if (!dl.CastShadow) return false;
			dir = glm::normalize(dir);
			found = true;
			break;
		}
		if (!found) return false;

		// 沿光线方向看向原点；若光线接近竖直，改用 Z 轴确定画面的上方。
		glm::vec3 up = glm::abs(dir.y) > 0.95f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
		glm::vec3 lightPos = -dir * 30.0f;
		glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), up);

		// 覆盖范围增大后，每个像素对应更大的表面，阴影细节会减少。
		const float half = s_ShadowSettings.OrthoHalfSize;
		glm::mat4 lightProj = glm::ortho(-half, half, -half, half, -50.0f, 50.0f);

		outMatrix   = lightProj * lightView;
		return true;
	}

	// ---- 阴影图绘制 ----
	// 从灯光方向画深度，结束后切回主画面。
	static void RunShadowPass(const glm::mat4& lightSpaceMatrix)
	{
		GLint  prevFBO = 0;
		GLint  prevViewport[4];
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);
		glGetIntegerv(GL_VIEWPORT, prevViewport);

		s_ShadowFBO->Bind();
		glClear(GL_DEPTH_BUFFER_BIT);

		Renderer3D::SetShadowData(lightSpaceMatrix, glm::vec4(0.0f) /* 画阴影图时只需光源矩阵 */);

		Renderer3D::ExecuteShadowPass(s_ShadowDepthShader);

		glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
	}

	// ---- 从方向光绘制反射阴影图 ----
	// 从方向光看场景，记录受光表面的位置、朝向和反射光。关闭阴影开关后，RSM 仍用这个光源矩阵绘图。
	// 画完后回到主画面的绘制目标，后续用这些表面估算反射光。
	static void RunRSMPass(const glm::mat4& lightSpaceMatrix)
	{
		if (!s_RSMFBO || !s_RSMDepthShader)
			return;

		GLint prevFBO = 0;
		GLint prevViewport[4];
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);
		glGetIntegerv(GL_VIEWPORT, prevViewport);

		s_RSMFBO->Bind();
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		Renderer3D::SetShadowData(lightSpaceMatrix, glm::vec4(0.0f));

		// 材质参数和底色贴图决定表面能反射多少光，透明裁剪也在这次绘制中执行。
		Renderer3D::ExecuteRSMPass(s_RSMDepthShader);

		glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
	}

	// ---- 本帧间接光 ----
	// 用预写的深度和法线还原当前表面，再从光源图中选取反射点，计算它收到的反射光。
	static void RunRSMIndirectPass(int w, int h,
	                               const glm::mat4& viewProj,
	                               GLint sceneDepthTex,
	                               GLint viewNormalTex)
	{
		if (!s_RSMIndirectShader || !s_RSMParamsUBO || !s_RSMIndirectParamsUBO || !s_RSMFBO) return;

		// 间接光图每个像素对应主画面上的一个像素。
		if (!s_RSMIndirectFBO)
		{
			FramebufferSpecification spec;
			spec.Width       = uint32_t(w);
			spec.Height      = uint32_t(h);
			spec.Attachments = { FramebufferTextureFormat::RGBA16F };
			s_RSMIndirectFBO = Framebuffer::Create(spec);
		}
		else
		{
			const auto& sp = s_RSMIndirectFBO->GetSpecifictaion();
			if (int(sp.Width) != w || int(sp.Height) != h)
			{
				s_RSMIndirectFBO->Resize(uint32_t(w), uint32_t(h));
				s_RSMHistoryValid = false;  // 视口大小变了，旧图的像素对不上
			}
		}
		if (!s_RSMIndirectFBO) return;

		// 调整采样或光照参数后，从这一次算出的颜色开始重新叠加。
		const int numSamples = glm::clamp(s_RSMSettings.NumSamples, 0, (int)RSM_MAX_SAMPLES);
		const glm::vec4 header(float(numSamples), s_RSMSettings.Intensity,
		                       s_RSMSettings.SampleRadius, 2.0f * s_ShadowSettings.OrthoHalfSize);
		if (s_RSMParameters.Header != header)
		{
			s_RSMHistoryValid = false;
			s_RSMParameters.Header = header;
			// 把圆盘分成面积相等的圆环，每个环取一个点。圆的面积与半径的平方成正比，所以下面要开平方。
			constexpr float GOLDEN_ANGLE = 2.39996322973f; // 黄金角，约 137.5°。相邻两点转过这个角，样本就分散在不同方向。
			for (int i = 0; i < numSamples; ++i)
			{
				const float radius = glm::sqrt((float(i) + 0.5f) / float(numSamples));
				const float theta = GOLDEN_ANGLE * float(i);
				s_RSMParameters.Samples[i] = glm::vec4(radius * glm::cos(theta), radius * glm::sin(theta), 1.0f, 0.0f);
			}
		}
		s_RSMParamsUBO->SetData(&s_RSMParameters, sizeof(s_RSMParameters));

		// 选择光源看到的表面数据，位置、朝向和反射通量必须来自同一次绘制。
		glBindTextureUnit(RSM_FLUX_SLOT,     s_RSMFBO->GetColorAttachmentRendererID(0));
		glBindTextureUnit(RSM_WORLDPOS_SLOT, s_RSMFBO->GetColorAttachmentRendererID(1));
		glBindTextureUnit(RSM_NORMAL_SLOT,   s_RSMFBO->GetColorAttachmentRendererID(2));
		glBindTextureUnit(RSM_DEPTH_SLOT,    s_RSMFBO->GetDepthAttachmentRendererID());
		// 选择主相机的深度与法线，用来找接收反射光的表面。
		glBindTextureUnit(RSM_INDIRECT_SCENE_DEPTH_SLOT, (GLuint)sceneDepthTex);
		glBindTextureUnit(RSM_INDIRECT_VIEW_NORMAL_SLOT, (GLuint)viewNormalTex);

		s_RSMIndirectFBO->Bind();
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_DEPTH_TEST);

		GPURSMIndirectParams iParams{};
		iParams.InvViewProj = glm::inverse(viewProj);
		iParams.FrameIndex  = (int)s_FrameIndex;
		s_RSMIndirectParamsUBO->SetData(&iParams, sizeof(iParams));

		s_RSMIndirectShader->Bind();

		DrawFullscreenTriangle();

		glEnable(GL_DEPTH_TEST);
	}

	// ---- 混合前后两帧的间接光 ----
	// 把本帧间接光与上次结果混合以减轻噪点。两张结果图轮换，读旧图时只写另一张。
	static void RunRSMTemporalResolvePass(int w, int h,
	                                      const glm::mat4& viewProj,
	                                      GLint sceneDepthTex)
	{
		if (!s_RSMTemporalResolveShader || !s_RSMTemporalParamsUBO || !s_RSMIndirectFBO) return;

		// 视口变大或变小时，两张保存反射光的图也要一起调整大小。
		for (int i = 0; i < 2; ++i)
		{
			if (!s_RSMHistoryFBO[i])
			{
				FramebufferSpecification spec;
				spec.Width       = uint32_t(w);
				spec.Height      = uint32_t(h);
				spec.Attachments = { FramebufferTextureFormat::RGBA16F };
				s_RSMHistoryFBO[i] = Framebuffer::Create(spec);
			}
			else
			{
				const auto& sp = s_RSMHistoryFBO[i]->GetSpecifictaion();
				if (int(sp.Width) != w || int(sp.Height) != h)
					s_RSMHistoryFBO[i]->Resize(uint32_t(w), uint32_t(h));
			}
		}
		if (!s_RSMHistoryFBO[0] || !s_RSMHistoryFBO[1]) return;

		int writeIdx = s_RSMHistoryIndex;
		int readIdx  = 1 - s_RSMHistoryIndex;

		s_RSMHistoryFBO[writeIdx]->Bind();
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_DEPTH_TEST);

		s_RSMTemporalResolveShader->Bind();

		// 本帧独立算出的间接光，与上一帧已经混合过的结果。
		glBindTextureUnit(0, s_RSMIndirectFBO->GetColorAttachmentRendererID(0));
		glBindTextureUnit(1, s_RSMHistoryFBO[readIdx]->GetColorAttachmentRendererID(0));
		// 当前深度用于还原表面位置，再找它上一帧出现在哪个像素。
		glBindTextureUnit(2, (GLuint)sceneDepthTex);

		GPURSMTemporalParams tParams{};
		tParams.InvViewProj  = glm::inverse(viewProj);
		tParams.PrevViewProj = s_PrevViewProj;
		tParams.BlendAlpha   = s_RSMBlendAlpha;
		tParams.ResetHistory = s_RSMHistoryValid ? 0 : 1;
		s_RSMTemporalParamsUBO->SetData(&tParams, sizeof(tParams));

		DrawFullscreenTriangle();

		glEnable(GL_DEPTH_TEST);

		// 翻转后索引指向下次写入目标；本帧结果位于 1-s_RSMHistoryIndex。
		s_RSMHistoryIndex = 1 - s_RSMHistoryIndex;
		s_PrevViewProj    = viewProj;
		s_RSMHistoryValid = true;
	}

	// ---- 主画面读取间接光 ----
	// 开启时选最近一次混合结果；关闭或尚无结果时选黑图，避免沿用上次的光照。
	static void BindRSMResources()
	{
		const bool active = s_RSMSettings.Enabled && s_RSMHistoryValid
			&& s_RSMHistoryFBO[0] && s_RSMHistoryFBO[1];

		if (active)
		{
			int readIdx = 1 - s_RSMHistoryIndex;
			glBindTextureUnit(RSM_RESOLVED_SLOT,
				s_RSMHistoryFBO[readIdx]->GetColorAttachmentRendererID(0));
		}
		else
		{
			glBindTextureUnit(RSM_RESOLVED_SLOT, s_RSMBlackTex);
		}
	}

	// ---- 深度预览 ----
	// 读回主画面的深度，按本帧最近到最远的物体拉伸成灰度图。灰度只供预览，不能直接换算成米。
	static void RunDepthVizPass()
	{
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		const int w = viewport[2], h = viewport[3];
		if (w <= 0 || h <= 0) return;

		// 面板显示的是缩小后的图。这里仍要读回整张深度图，GPU 还在绘制时就得等它画完。
		int maxW = 512;
		int vw = std::min(w, maxW);
		int vh = std::max(1, h * vw / w);

		// 从刚才绘制场景的目标读取深度。
		GLint drawFBO = 0, readFBO = 0;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO);
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFBO);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, drawFBO);

		static std::vector<float> depthBuf;
		depthBuf.resize(size_t(w) * h);
		glReadPixels(0, 0, w, h, GL_DEPTH_COMPONENT, GL_FLOAT, depthBuf.data());

		// 只用实际画到的物体确定黑白范围，忽略背景和紧贴远裁剪面的像素。
		float dMin = 1.0f, dMax = 0.0f;
		for (float d : depthBuf)
		{
			if (d >= 0.9999f) continue;
			if (d < dMin) dMin = d;
			if (d > dMax) dMax = d;
		}
		bool hasGeo = dMin <= dMax;
		float range = std::max(dMax - dMin, 0.000001f);

		// 按宽高比例从原深度图取一个像素。例如宽度减半时，每隔两列取一次深度。
		static std::vector<uint8_t> pixels;
		pixels.resize(size_t(vw) * vh);
		for (int y = 0; y < vh; ++y)
		{
			int srcY = y * h / vh;
			for (int x = 0; x < vw; ++x)
			{
				int srcX = x * w / vw;
				float d  = depthBuf[size_t(srcY) * w + srcX];
				float g;
				if (d >= 0.9999f || !hasGeo)
				{
					g = 1.0f; // 天空 = 白
				}
				else
				{
					g = (d - dMin) / range;
				}
				if (g < 0.0f) g = 0.0f;
				if (g > 1.0f) g = 1.0f;
				pixels[size_t(y) * vw + x] = uint8_t(g * 255.0f);
			}
		}

		if (s_DepthVizTex == 0 || int(s_DepthVizTexW) != vw || int(s_DepthVizTexH) != vh)
		{
			if (s_DepthVizTex) { glDeleteTextures(1, &s_DepthVizTex); s_DepthVizTex = 0; }
			glCreateTextures(GL_TEXTURE_2D, 1, &s_DepthVizTex);
			glTextureStorage2D(s_DepthVizTex, 1, GL_R8, vw, vh);
			glTextureParameteri(s_DepthVizTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTextureParameteri(s_DepthVizTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTextureParameteri(s_DepthVizTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTextureParameteri(s_DepthVizTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			// 预览纹理只有红色通道。让红、绿、蓝都读取这个值，面板上看到的就是灰度图。
			GLint swizzle[4] = { GL_RED, GL_RED, GL_RED, GL_ONE };
			glTextureParameteriv(s_DepthVizTex, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
			s_DepthVizTexW = (uint32_t)vw;
			s_DepthVizTexH = (uint32_t)vh;
		}

		GLint unpackAlignment;
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTextureSubImage2D(s_DepthVizTex, 0, 0, 0, vw, vh, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
		glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);

		s_LastPrepassDepthTex = s_DepthVizTex;
	}

	// ---- 屏幕空间环境遮蔽 ----
	// 深度预写给出每个可见表面离相机多远、朝向哪里。用它们计算环境光在各处被挡住多少。
	static void RunSSAOPass(const glm::mat4& projection)
	{
		if (!s_SSAOShader || !s_SSAOFBO) return;

		GLint prevFBO = 0;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);
		GLint prevViewport[4];
		glGetIntegerv(GL_VIEWPORT, prevViewport);
		int w = prevViewport[2];
		int h = prevViewport[3];
		if (w <= 0 || h <= 0) return;

		GLint depthTex = 0, normalTex = 0;
		glGetNamedFramebufferAttachmentParameteriv(prevFBO,
			GL_DEPTH_ATTACHMENT,
			GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depthTex);
		glGetNamedFramebufferAttachmentParameteriv(prevFBO,
			GL_COLOR_ATTACHMENT2,
			GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &normalTex);
		if (depthTex == 0 || normalTex == 0)
			return;

		s_LastViewNormalTex   = (uint32_t)normalTex;

		const auto& spec = s_SSAOFBO->GetSpecifictaion();
		if (int(spec.Width) != w || int(spec.Height) != h)
			s_SSAOFBO->Resize(uint32_t(w), uint32_t(h));

		// 着色器用逆投影从深度恢复表面位置，再把周围采样点投回屏幕比较深度。
		GPUSSAOParams p{};
		p.Projection    = projection;
		p.InvProjection = glm::inverse(projection);
		int kSize = s_SSAOSettings.KernelSize;
		if (kSize < 1) kSize = 1;
		if (kSize > (int)SSAO_KERNEL_SIZE) kSize = (int)SSAO_KERNEL_SIZE;
		p.Params     = glm::vec4(s_SSAOSettings.Radius,
		                         s_SSAOSettings.Bias,
		                         s_SSAOSettings.Power,
		                         float(kSize));
		p.NoiseScale = glm::vec4(float(w) / 4.0f, float(h) / 4.0f, 0.0f, 0.0f);
		for (uint32_t i = 0; i < SSAO_KERNEL_SIZE; ++i)
			p.Kernel[i] = s_SSAOKernel[i];
		s_SSAOParamsUBO->SetData(&p, sizeof(GPUSSAOParams));

		s_SSAOFBO->Bind();
		glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_DEPTH_TEST);

		s_SSAOShader->Bind();
		glBindTextureUnit(SSAO_DEPTH_SLOT,  (GLuint)depthTex);
		glBindTextureUnit(SSAO_NORMAL_SLOT, (GLuint)normalTex);
		glBindTextureUnit(SSAO_NOISE_SLOT,  s_SSAONoiseTex);

		glBindVertexArray(s_SSAOQuadVAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);

		Renderer3D::LogExternalDrawCall(
			Renderer3D::DrawCallInfo::PassType::SSAO, 3);

		glEnable(GL_DEPTH_TEST);

		glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

		// 主画面随后读取遮蔽图，只压暗环境光。
		glBindTextureUnit(SSAO_MAP_SLOT, s_SSAOFBO->GetColorAttachmentRendererID(0));
	}

	static void BindSSAOWhiteFallback()
	{
		if (s_SSAOWhiteTex)
			glBindTextureUnit(SSAO_MAP_SLOT, s_SSAOWhiteTex);
	}

	// ---- 场景几何 ----
	static const Ref<Model>& SelectModel(ModelRendererComponent& component, const glm::mat4& modelToClip)
	{
		auto& level = component.ActiveLOD;
		level = std::min(level, component.LODs.size());
		if (component.LODs.empty()) return component.ModelAsset;

		const float height = component.ModelAsset->GetScreenHeight(modelToClip);
		// 例如设为 25%，兔子小于画面高度的 22.5% 才换简模，大于 27.5% 才换回。中间保留上一档。
		while (level < component.LODs.size() && height < .9f * component.LODs[level].ScreenHeight) ++level;
		while (level > 0 && height > 1.1f * component.LODs[level - 1].ScreenHeight) --level;
		// 远处用的网格文件丢失时，向上找一份仍能画的模型。
		while (level > 0)
		{
			auto& lod = component.LODs[level - 1];
			if (!lod.ModelAsset) lod.ModelAsset = AssetManager::Load<Model>(lod.ModelID);
			if (lod.ModelAsset) return lod.ModelAsset;
			--level;
		}
		return component.ModelAsset;
	}

	// 编辑器和游戏相机共用这次查询。第一次遇到模型时加载，以后直接用组件里的引用。
	template<typename Draw>
	static void ForEachMesh(Scene& scene, const glm::mat4& lodViewProjection, Draw draw)
	{
		auto& registry = scene.GetRegistry();

		// 直接创建的单个网格用组件上的材质、贴图或颜色。
		registry.view<TransformComponent, MeshRendererComponent>().each(
			[&](auto entity, TransformComponent& transform, MeshRendererComponent& mesh)
		{
			if (!mesh.MeshData)
				return;
			auto material = mesh.Mat;
			if (!material)
				material = mesh.Texture ? Material::Create(mesh.Texture) : Material::Create(mesh.Color);
			draw(mesh.MeshData, transform.GetTransform(), material, static_cast<int>(entity), 1.0f);
		});

		registry.view<TransformComponent, SkeletonAnimationComponent>().each(
			[&](auto entity, TransformComponent& transform, SkeletonAnimationComponent& player)
		{
			if (!player.Clip) return;
			if (!player.BoneMesh)
			{
				player.BoneMesh = Mesh::CreateCube();
				player.BoneMaterial = Material::Create(glm::vec4(.84f, .52f, .22f, 1.f));
			}
			const glm::mat4 world = transform.GetTransform();
			for (const glm::mat4& bone : player.Bones)
				draw(player.BoneMesh, world * bone, player.BoneMaterial, static_cast<int>(entity), 1.f);
		});

		registry.view<RagdollComponent>().each([&](entt::entity entity, RagdollComponent& character) {
			if (!character.LimbMesh)
			{
				character.LimbMesh = Mesh::CreateCube();
				character.LimbMaterial = Material::Create(glm::vec4(.84f, .52f, .22f, 1.f));
			}
			for (const glm::mat4& limb : scene.GetRagdollMeshes(entity))
				draw(character.LimbMesh, limb, character.LimbMaterial, static_cast<int>(entity), 1.f);
		});

		// 文件导入的模型可能含多个部件，实体的变换对它们全部生效。
		registry.view<TransformComponent, ModelRendererComponent>().each(
			[&](auto entity, TransformComponent& transform, ModelRendererComponent& component)
		{
			if (!component.ModelAsset && component.ModelID)
				component.ModelAsset = AssetManager::Load<Model>(component.ModelID);
			if (!component.ModelAsset)
				return;

			const glm::mat4 world = transform.GetTransform();
			auto* player = registry.try_get<ModelAnimationComponent>(entity);
			if (player) player->Update(component.ModelAsset, 0.f);
			const bool posed = player && !player->GetMeshes().empty();
			const auto& model = posed ? *component.ModelAsset : *SelectModel(component, lodViewProjection * world);
			const auto& parts = model.GetSubMeshes();
			Ref<Material> fallback;

			for (size_t i = 0; i < parts.size(); ++i)
			{
				const auto& part = parts[i];
				if (!part.MeshData)
					continue;

				const auto& material = model.GetMaterial(i, component.OverrideMaterials);
				if (!material && !fallback)
					fallback = Material::Create(component.Color);

				const float instanceAlpha = material && material->Alpha == AlphaMode::Mask ? component.Color.a : 1.0f;
				const Ref<Mesh>& mesh = posed ? player->GetMeshes()[i] : part.MeshData;
				const glm::mat4& local = posed ? player->GetTransform(i) : part.LocalTransform;
				draw(mesh, world * local, material ? material : fallback,
					static_cast<int>(entity), instanceAlpha);
			}
		});
	}

	static void CollectGeometry(Scene& scene, const glm::mat4& viewProjection)
	{
		ForEachMesh(scene, viewProjection, [](const Ref<Mesh>& mesh, const glm::mat4& world,
			const Ref<Material>& material, int id, float instanceAlpha)
		{
			Ref<Material> drawMaterial = material;
			if (instanceAlpha != 1.0f)
			{
				// 两个实体可以共用树叶材质。只改这次绘制的透明度，贴图仍共用。
				drawMaterial = material->Clone();
				drawMaterial->BaseColor.a *= instanceAlpha;
			}
			Renderer3D::DrawMesh(mesh, world, drawMaterial, id);
		});
	}

	void SceneRenderer::DrawGeometry(Scene& scene, CommandList& commands, const glm::mat4& viewProjection,
		const glm::mat4* lodViewProjection)
	{
		ForEachMesh(scene, lodViewProjection ? *lodViewProjection : viewProjection, [&](const Ref<Mesh>& mesh, const glm::mat4& world,
			const Ref<Material>& material, int id, float instanceAlpha)
		{
			const auto texture = material->BaseColorMap ? material->BaseColorMap->GetSampledTexture() : nullptr;
			SurfaceMaterial surface;
			surface.Normal = material->NormalMap ? material->NormalMap->GetSampledTexture() : nullptr;
			surface.Opacity = material->AlphaMap ? material->AlphaMap->GetSampledTexture() : nullptr;
			surface.MetallicRoughnessMap = material->MetallicRoughnessMap ? material->MetallicRoughnessMap->GetSampledTexture() : nullptr;
			surface.Metallic = material->Metallic;
			surface.Roughness = material->Roughness;
			surface.Blend = material->Alpha == AlphaMode::Blend;
			surface.DoubleSided = material->DoubleSided;
			surface.Emission = material->Emission;
			surface.EmissiveMap = material->EmissiveMap ? material->EmissiveMap->GetSampledTexture() : nullptr;
			surface.RefractionIndex = material->RefractionIndex;
			surface.Thickness = material->Thickness;
			if (material->Alpha == AlphaMode::Mask) surface.AlphaCutoff = material->AlphaCutoff;
			glm::vec4 color = material->BaseColor;
			color.a *= instanceAlpha;
			commands.DrawIndexed(mesh->GetGeometry(), texture, viewProjection, world, color, id, surface);
		});
	}

	// ---- 编辑器帧 ----
	// 先从光源方向画阴影和反射阴影图，再从主相机画深度与法线。有了两组数据才能算遮蔽与间接光。
	void SceneRenderer::RenderScene(Scene& scene, const EditorCamera& camera)
	{
		IBLPrecompute::UseEnvironment(scene.GetEnvironment());
		UpdateRSMView(scene, camera.GetProjection(), &camera);
		entt::registry& registry = scene.GetRegistry();

		// 2D 颜色含透明度；上一帧后处理会关闭混合，这里重新设置。
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// ---- 精灵与圆 ----
		Renderer2D::BeginScene(camera);

		// 遍历所有带 Transform + Sprite 的实体，提交 2D 四边形
		{
			auto view = registry.view<TransformComponent, SpriteRendererComponent>();
			view.each([&](auto entity, TransformComponent& transform, SpriteRendererComponent& sprite)
			{
				// 如果实体同时拥有纹理动画组件且有纹理，用子纹理渲染当前帧
				if (registry.all_of<TextureAnimationComponent>(entity) && sprite.Texture)
				{
					auto& anim = registry.get<TextureAnimationComponent>(entity);
					if (anim.Columns > 0 && anim.Rows > 0)
					{
						float cellW = (float)sprite.Texture->GetWidth()  / (float)anim.Columns;
						float cellH = (float)sprite.Texture->GetHeight() / (float)anim.Rows;
						int col = anim.CurrentFrame % anim.Columns;
						int row = anim.CurrentFrame / anim.Columns;
						auto subTex = SubTexture2D::CreateFromCoords(
							sprite.Texture,
							{ (float)col, (float)(anim.Rows - 1 - row) },
							{ cellW, cellH });
						Renderer2D::DrawQuad(transform.GetTransform(), subTex, sprite.TilingFactor, sprite.Color, (int)entity);
						return;
					}
				}
				Renderer2D::DrawSprite(transform.GetTransform(), sprite, (int)entity);
			});
		}

		// 遍历所有带 Transform + Circle 的实体，提交 2D 圆
		{
			auto view = registry.view<TransformComponent, CircleRendererComponent>();
			view.each([&](auto entity, TransformComponent& transform, CircleRendererComponent& circle)
			{
				Renderer2D::DrawCircle(transform.GetTransform(), circle.Color, circle.Thickness, circle.Fade, (int)entity);
			});
		}

		Renderer2D::EndScene();

		// 2D 绘制用过透明混合。画 3D 网格前先关掉，避免把原图的颜色混进不透明表面。
		glDisable(GL_BLEND);

		CollectAndUploadLights(scene);

		// ---- 网格与模型 ----
		Renderer3D::BeginScene(camera);
		if (!s_IBLEnabled)
			Renderer3D::OverrideIBLFlag(false);

		CollectGeometry(scene, camera.GetViewProjection());

		// ---- 阴影与间接光 ----
		// 阴影和反射阴影图从同一盏方向光看场景，开关各自独立。
		glm::mat4 lightSpaceMatrix(1.0f);
		bool hasDirLight = TryBuildLightSpaceMatrix(scene, lightSpaceMatrix);
		bool runShadow   = s_ShadowEnabled && hasDirLight;
		bool runRSM      = s_RSMSettings.Enabled && hasDirLight;

		if (runShadow)
			RunShadowPass(lightSpaceMatrix);

		if (runRSM)
			RunRSMPass(lightSpaceMatrix);

		if (hasDirLight)
		{
			glm::vec4 shadowParams(
				s_ShadowSettings.DepthBias,
				s_ShadowSettings.NormalBias,
				1.0f / float(SHADOW_MAP_SIZE),
				runShadow ? float(s_ShadowSettings.Mode) : 0.0f);
			glm::vec4 shadowParams2(
				s_ShadowSettings.PcfRadius,
				s_ShadowSettings.LightSize,
				0.0f, 0.0f);
			Renderer3D::SetShadowData(lightSpaceMatrix, shadowParams, shadowParams2);
		}

		if (runShadow)
		{
			glBindTextureUnit(SHADOW_MAP_SLOT, s_ShadowFBO->GetDepthAttachmentRendererID());
		}

		// 即使单独关掉深度预写，遮蔽和间接光开启时仍要为它们准备深度和法线。
		bool runPrepass = s_ZPrepassEnabled || s_SSAOEnabled || runRSM;
		if (runPrepass)
		{
			Renderer3D::ExecuteDepthPrepass(s_DepthPrepassShader);

			RunDepthVizPass();

			// 主画面重画同一表面时，深度和刚才写的相等也要允许通过。
			glDepthFunc(GL_LEQUAL);
			glDepthMask(GL_FALSE);
		}

		if (s_SSAOEnabled)
			RunSSAOPass(camera.GetProjection());
		else
			BindSSAOWhiteFallback();

		// 用当前深度和法线计算反射光，再和上一帧的结果混合。
		if (runRSM && runPrepass)
		{
			GLint mainFBO = 0;
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &mainFBO);
			GLint vp[4];
			glGetIntegerv(GL_VIEWPORT, vp);
			int w = vp[2], h = vp[3];

			GLint depthTex = 0, normalTex = 0;
			glGetNamedFramebufferAttachmentParameteriv(mainFBO,
				GL_DEPTH_ATTACHMENT,
				GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depthTex);
			glGetNamedFramebufferAttachmentParameteriv(mainFBO,
				GL_COLOR_ATTACHMENT2,
				GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &normalTex);

			if (w > 0 && h > 0 && depthTex && normalTex)
			{
				const glm::mat4 vp_mat = camera.GetViewProjection();
				RunRSMIndirectPass(w, h, vp_mat, depthTex, normalTex);
				RunRSMTemporalResolvePass(w, h, vp_mat, depthTex);

				// 两张间接光图都画完了，切回场景继续绘制。
				glBindFramebuffer(GL_FRAMEBUFFER, mainFBO);
				glViewport(vp[0], vp[1], vp[2], vp[3]);
			}
		}
		else if (!runRSM)
		{
			// 关掉间接光后，旧图可能和场景不再相符。下次打开要重新画。
			s_RSMHistoryValid = false;
		}

		// 为主绘制选好间接光图；关闭时改用黑图。
		BindRSMResources();

		Renderer3D::DrawOpaque();

		++s_FrameIndex;

		if (runPrepass)
		{
			glDepthMask(GL_TRUE);
			glDepthFunc(GL_LESS);
		}

		// 不透明物体画完后，用天空填上余下的背景。
		if (scene.IsSkyVisible())
			IBLPrecompute::RenderSkybox(camera.GetViewMatrix(), camera.GetProjection());

		Renderer3D::DrawTransparency();

		// 画包围盒，方便核对哪些物体被相机剔除了。
		if (Renderer3D::GetDebugCullingVizEnabled())
		{
			Renderer2D::BeginScene(camera);
			Renderer3D::DrawDebugAABBs();
			Renderer2D::EndScene();
		}

		// 后处理在全部 HDR 绘制结束后执行。
		s_PostProcess->Render(s_MainSceneColorTex, s_MainSceneWidth, s_MainSceneHeight);
	}

	// ---- 运行时帧 ----
	// 与编辑器的绘制顺序相同，观察矩阵由相机的世界变换求逆得到。
	void SceneRenderer::RenderRuntime(Scene& scene, const glm::mat4& projection, const glm::mat4& cameraTransform)
	{
		IBLPrecompute::UseEnvironment(scene.GetEnvironment());
		UpdateRSMView(scene, projection);
		entt::registry& registry = scene.GetRegistry();

		// 2D 颜色含透明度；上一帧后处理会关闭混合，这里重新设置。
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// 2D 渲染
		Renderer2D::BeginScene(projection, cameraTransform);

		{
			auto view = registry.view<TransformComponent, SpriteRendererComponent>();
			view.each([&](auto entity, TransformComponent& transform, SpriteRendererComponent& sprite)
			{
				// 如果实体同时拥有纹理动画组件且有纹理，用子纹理渲染当前帧
				if (registry.all_of<TextureAnimationComponent>(entity) && sprite.Texture)
				{
					auto& anim = registry.get<TextureAnimationComponent>(entity);
					if (anim.Columns > 0 && anim.Rows > 0)
					{
						float cellW = (float)sprite.Texture->GetWidth()  / (float)anim.Columns;
						float cellH = (float)sprite.Texture->GetHeight() / (float)anim.Rows;
						int col = anim.CurrentFrame % anim.Columns;
						int row = anim.CurrentFrame / anim.Columns;
						auto subTex = SubTexture2D::CreateFromCoords(
							sprite.Texture,
							{ (float)col, (float)(anim.Rows - 1 - row) },
							{ cellW, cellH });
						Renderer2D::DrawQuad(transform.GetTransform(), subTex, sprite.TilingFactor, sprite.Color, (int)entity);
						return;
					}
				}
				Renderer2D::DrawSprite(transform.GetTransform(), sprite, (int)entity);
			});
		}

		{
			auto view = registry.view<TransformComponent, CircleRendererComponent>();
			view.each([&](auto entity, TransformComponent& transform, CircleRendererComponent& circle)
			{
				Renderer2D::DrawCircle(transform.GetTransform(), circle.Color, circle.Thickness, circle.Fade, (int)entity);
			});
		}

		// 2D 粒子渲染
		{
			auto view = registry.view<ParticleEmitterComponent>();
			for (auto e : view)
			{
				auto& emitter = registry.get<ParticleEmitterComponent>(e);
				if (!emitter.RuntimeParticleSystem)
					continue;
				ParticleSystem* ps = (ParticleSystem*)emitter.RuntimeParticleSystem;
				ps->RenderBatch();
			}
		}

		Renderer2D::EndScene();

		// 2D 绘制用过透明混合。画 3D 网格前先关掉，避免把原图的颜色混进不透明表面。
		glDisable(GL_BLEND);

		// 灯光收集
		CollectAndUploadLights(scene);

		// 3D 渲染
		glm::mat4 viewProj3D = projection * glm::inverse(cameraTransform);
		glm::vec3 cameraPos  = glm::vec3(cameraTransform[3]); // 从变换矩阵提取位移列
		Renderer3D::BeginScene(viewProj3D, cameraPos, glm::inverse(cameraTransform));
		if (!s_IBLEnabled)
			Renderer3D::OverrideIBLFlag(false);

		CollectGeometry(scene, viewProj3D);

		// ---- 阴影与间接光 ----
		glm::mat4 lightSpaceMatrix_rt(1.0f);
		bool hasDirLight_rt = TryBuildLightSpaceMatrix(scene, lightSpaceMatrix_rt);
		bool runShadow_rt   = s_ShadowEnabled && hasDirLight_rt;
		bool runRSM_rt      = s_RSMSettings.Enabled && hasDirLight_rt;

		if (runShadow_rt)
			RunShadowPass(lightSpaceMatrix_rt);

		if (runRSM_rt)
			RunRSMPass(lightSpaceMatrix_rt);

		if (hasDirLight_rt)
		{
			glm::vec4 shadowParams(
				s_ShadowSettings.DepthBias,
				s_ShadowSettings.NormalBias,
				1.0f / float(SHADOW_MAP_SIZE),
				runShadow_rt ? float(s_ShadowSettings.Mode) : 0.0f);
			glm::vec4 shadowParams2(
				s_ShadowSettings.PcfRadius,
				s_ShadowSettings.LightSize,
				0.0f, 0.0f);
			Renderer3D::SetShadowData(lightSpaceMatrix_rt, shadowParams, shadowParams2);
		}

		if (runShadow_rt)
			glBindTextureUnit(SHADOW_MAP_SLOT, s_ShadowFBO->GetDepthAttachmentRendererID());

		// 遮蔽和间接光共用这次从主相机画出的深度与法线。
		{
			bool runPrepass = s_ZPrepassEnabled || s_SSAOEnabled || runRSM_rt;
			if (runPrepass)
			{
				Renderer3D::ExecuteDepthPrepass(s_DepthPrepassShader);

				// 深度可视化
				RunDepthVizPass();

				glDepthFunc(GL_LEQUAL);
				glDepthMask(GL_FALSE);
			}

			if (s_SSAOEnabled)
				RunSSAOPass(projection);
			else
				BindSSAOWhiteFallback();

			// 先算出当前表面收到的反射光，再与上一帧保存在图里的间接光混合，减轻噪点。
			if (runRSM_rt && runPrepass)
			{
				GLint mainFBO = 0;
				glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &mainFBO);
				GLint vp[4];
				glGetIntegerv(GL_VIEWPORT, vp);
				int w = vp[2], h = vp[3];

				GLint depthTex = 0, normalTex = 0;
				glGetNamedFramebufferAttachmentParameteriv(mainFBO,
					GL_DEPTH_ATTACHMENT,
					GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depthTex);
				glGetNamedFramebufferAttachmentParameteriv(mainFBO,
					GL_COLOR_ATTACHMENT2,
					GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &normalTex);

				if (w > 0 && h > 0 && depthTex && normalTex)
				{
					const glm::mat4 vp_rt = projection * glm::inverse(cameraTransform);
					RunRSMIndirectPass(w, h, vp_rt, depthTex, normalTex);
					RunRSMTemporalResolvePass(w, h, vp_rt, depthTex);

					glBindFramebuffer(GL_FRAMEBUFFER, mainFBO);
					glViewport(vp[0], vp[1], vp[2], vp[3]);
				}
			}
			if (!runRSM_rt)
				s_RSMHistoryValid = false;

			BindRSMResources();

			Renderer3D::DrawOpaque();
			++s_FrameIndex;

			if (runPrepass)
			{
				glDepthMask(GL_TRUE);
				glDepthFunc(GL_LESS);
			}
		}

		if (scene.IsSkyVisible())
			IBLPrecompute::RenderSkybox(glm::inverse(cameraTransform), projection);

		Renderer3D::DrawTransparency();

		// 剪裁可视化调试
		if (Renderer3D::GetDebugCullingVizEnabled())
		{
			Renderer2D::BeginScene(projection, cameraTransform);
			Renderer3D::DrawDebugAABBs();
			Renderer2D::EndScene();
		}

		// 后处理在全部 HDR 绘制结束后执行。
		s_PostProcess->Render(s_MainSceneColorTex, s_MainSceneWidth, s_MainSceneHeight);
	}

	// ---- 场景灯光 ----
	void SceneRenderer::CollectAndUploadLights(Scene& scene)
	{
		entt::registry& registry = scene.GetRegistry();

		GPULightUBO header{};
		GPULightData dirLights[MAX_DIRECTIONAL_LIGHTS]{};
		GPULightData pointLights[MAX_POINT_LIGHTS]{};

		// 方向随实体旋转，在世界空间中参与光照计算。
		{
			auto view = registry.view<TransformComponent, DirectionalLightComponent>();
			view.each([&](auto entity, TransformComponent& tc, DirectionalLightComponent& dl)
			{
				if (header.NumDirectionalLights >= (int)MAX_DIRECTIONAL_LIGHTS) return;
				auto& light = dirLights[header.NumDirectionalLights];
				glm::vec3 dir = dl.WorldDirection(tc);
				if (glm::dot(dir, dir) < 1e-6f) return;
				dir = glm::normalize(dir);
				light.Direction = glm::vec4(dir, 0.0f);
				light.Color     = glm::vec4(dl.Color, dl.Intensity);
				light.Position  = glm::vec4(0.0f);                // 方向光无位置
				light.CastShadow = dl.CastShadow ? 1 : 0;
				header.NumDirectionalLights++;
			});
		}

		// RSM 使用第一盏方向光。颜色、强度或方向改变时不能继续混入旧光照。
		if (s_RSMLightDirection != dirLights[0].Direction || s_RSMLightColor != dirLights[0].Color)
			s_RSMHistoryValid = false;
		s_RSMLightDirection = dirLights[0].Direction;
		s_RSMLightColor = dirLights[0].Color;

		{
			auto view = registry.view<TransformComponent, PointLightComponent>();
			view.each([&](auto entity, TransformComponent& tc, PointLightComponent& pl)
			{
				if (header.NumPointLights >= (int)MAX_POINT_LIGHTS) return;
				auto& light = pointLights[header.NumPointLights];
				light.Direction = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
				light.Color     = glm::vec4(pl.Color, pl.Intensity);
				light.Position  = glm::vec4(tc.Translation, pl.Radius);
				light.CastShadow = pl.CastShadow ? 1 : 0;
				header.NumPointLights++;
			});
		}

		// 着色器先读两类灯的数量，再按各自的起始位置读取数据。
		uint32_t offset = 0;

		s_LightUBO->SetData(&header, sizeof(GPULightUBO), offset);
		offset += sizeof(GPULightUBO);

		// 点光源总是从预留的方向光数组之后开始，即使本帧没有方向光。
		if (header.NumDirectionalLights > 0)
			s_LightUBO->SetData(dirLights, header.NumDirectionalLights * sizeof(GPULightData), offset);
		offset += MAX_DIRECTIONAL_LIGHTS * sizeof(GPULightData);

		if (header.NumPointLights > 0)
			s_LightUBO->SetData(pointLights, header.NumPointLights * sizeof(GPULightData), offset);
	}

} // namespace Redflag
