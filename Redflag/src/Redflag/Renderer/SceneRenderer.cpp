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

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <random>

namespace Redflag {

	// ---- 灯光资源 ----
	static Ref<UniformBuffer> s_LightUBO;

	// ---- 阴影资源 ----
	// 单张 2048×2048 深度图，使用主绘制的第一盏有效方向光（须允许投影），正交半宽由面板设置。
	static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
	static constexpr uint32_t SHADOW_MAP_SLOT = 11; // 与主 Mesh shader 里的 binding = 11 对应

	static Ref<Framebuffer> s_ShadowFBO;
	static Ref<Shader>      s_ShadowDepthShader;
	static Ref<Shader>      s_DepthPrepassShader;

	// Shadow 调参：所有字段暴露给 ImGui 面板，运行时可改
	static ShadowSettings s_ShadowSettings;

	// IBL / Shadow / Z-Prepass 全局开关
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
	// 输入主目标深度与观察空间法线，输出独立 AO 纹理，在主 Pass 的环境光部分使用。
	static constexpr uint32_t SSAO_KERNEL_SIZE = 64;
	static constexpr uint32_t SSAO_UBO_BINDING  = 4;
	static constexpr uint32_t SSAO_MAP_SLOT     = 12; // 主 Mesh shader 读 AO 的槽
	static constexpr uint32_t SSAO_DEPTH_SLOT   = 13;
	static constexpr uint32_t SSAO_NORMAL_SLOT  = 14;
	static constexpr uint32_t SSAO_NOISE_SLOT   = 15;

	struct GPUSSAOParams
	{
		glm::mat4 Projection;
		glm::mat4 InvProjection;
		glm::vec4 Params;      // x=radius y=bias z=power w=kernelSize
		glm::vec4 NoiseScale;  // xy = viewportSize/4
		glm::vec4 Kernel[SSAO_KERNEL_SIZE];
	};

	static SSAOSettings      s_SSAOSettings;
	static Ref<Framebuffer>  s_SSAOFBO;
	static Ref<Shader>       s_SSAOShader;
	static Ref<UniformBuffer> s_SSAOParamsUBO;
	static uint32_t          s_SSAONoiseTex = 0;
	static uint32_t          s_SSAOWhiteTex = 0; // 关闭 SSAO 时绑到 slot 12
	static uint32_t          s_SSAOQuadVAO  = 0;
	static uint32_t          s_SSAOQuadVBO  = 0;
	static glm::vec4         s_SSAOKernel[SSAO_KERNEL_SIZE]{};
	// SSAO Pass 中从主 FBO 查到的 view-normal 纹理 id，留给 ImGui 面板预览
	static uint32_t          s_LastViewNormalTex    = 0;
	// Z-Prepass 的 R8 深度预览，按当帧深度范围拉伸，供 ImGui 显示
	static uint32_t          s_LastPrepassDepthTex  = 0;

	// 深度预览：CPU 读回原始深度 → 拉伸对比度 → 上传 R8 纹理
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

	// ---- RSM 时域累积 ----
	static constexpr uint32_t RSM_INDIRECT_SCENE_DEPTH_SLOT = 18;
	static constexpr uint32_t RSM_INDIRECT_VIEW_NORMAL_SLOT = 19;
	static constexpr uint32_t RSM_RESOLVED_SLOT             = 17;

	static Ref<Framebuffer>   s_RSMIndirectFBO;       // 本帧单独算出的间接光，与屏幕同尺寸
	static Ref<Framebuffer>   s_RSMHistoryFBO[2];     // 两张图交替读写，保留上次混合结果
	static int                s_RSMHistoryIndex = 0;  // 当前写入目标索引（0 或 1）
	static uint32_t           s_FrameIndex      = 0;  // 每帧转动采样点，避免总取到相同位置
	static glm::mat4          s_PrevViewProj    = glm::mat4(1.0f);
	static bool               s_RSMHistoryValid = false; // 能否继续使用上次结果
	static float              s_RSMBlendAlpha   = 0.9f;  // 混合时旧结果所占比例
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
		// 写完后索引已翻转，另一张图才是最近一次完成的混合结果。
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

	// CPU 侧灯光缓冲区，每帧填充后一次性上传
	// 布局：[GPULightUBO header (16B)] + [LightData[] 数组]
	static constexpr uint32_t LIGHT_UBO_SIZE =
		sizeof(GPULightUBO) +
		(MAX_DIRECTIONAL_LIGHTS + MAX_POINT_LIGHTS) * sizeof(GPULightData);

	// ---- 初始化与销毁 ----
	void SceneRenderer::Init()
	{
		s_LightUBO = UniformBuffer::Create(LIGHT_UBO_SIZE, LIGHT_UBO_BINDING);

		// ---- Shadow Pass 资源 ----
		// DEPTH32F depth-only FBO：无颜色附件，viewport 会绑为 ShadowMap 尺寸
		FramebufferSpecification shadowSpec;
		shadowSpec.Width  = SHADOW_MAP_SIZE;
		shadowSpec.Height = SHADOW_MAP_SIZE;
		shadowSpec.Attachments = { FramebufferTextureFormat::DEPTH32F };
		s_ShadowFBO = Framebuffer::Create(shadowSpec);

		s_ShadowDepthShader  = Shader::Create("assets/shaders/ShadowDepth.glsl");
		s_DepthPrepassShader = Shader::Create("assets/shaders/Depth_Prepass.glsl");

		// ---- SSAO 资源 ----
		// 单附件 RGBA8 FBO；后面主 Mesh shader 只读 .r 通道。初始尺寸与 Shadow
		// 无关，RenderScene 每帧会根据当前 viewport 做 Resize。
		FramebufferSpecification ssaoSpec;
		ssaoSpec.Width  = 1280;
		ssaoSpec.Height = 720;
		ssaoSpec.Attachments = { FramebufferTextureFormat::RGBA8 };
		s_SSAOFBO = Framebuffer::Create(ssaoSpec);

		s_SSAOShader    = Shader::Create("assets/shaders/SSAO.glsl");
		s_SSAOParamsUBO = UniformBuffer::Create(sizeof(GPUSSAOParams), SSAO_UBO_BINDING);

		// 生成 64 个半球样本（+Z 方向，余弦偏向原点）
		{
			std::default_random_engine gen(1337u);
			std::uniform_real_distribution<float> rnd01(0.0f, 1.0f);
			for (uint32_t i = 0; i < SSAO_KERNEL_SIZE; ++i)
			{
				glm::vec3 s(rnd01(gen) * 2.0f - 1.0f,
							rnd01(gen) * 2.0f - 1.0f,
							rnd01(gen));          // 只取上半球
				s = glm::normalize(s) * rnd01(gen);
				float scale = float(i) / float(SSAO_KERNEL_SIZE);
				// 二次重映射让样本偏向原点（靠近接收点的样本更密集）
				scale = 0.1f + (1.0f - 0.1f) * scale * scale;
				s *= scale;
				s_SSAOKernel[i] = glm::vec4(s, 0.0f);
			}
		}

		// 4×4 随机切线噪声（仅 xy 有效），用于 SSAO 里打散带状纹
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

		// 关闭 SSAO 时绑定的 1×1 全白纹理（避免主 shader 分支）
		{
			glCreateTextures(GL_TEXTURE_2D, 1, &s_SSAOWhiteTex);
			glTextureStorage2D(s_SSAOWhiteTex, 1, GL_RGBA8, 1, 1);
			uint32_t white = 0xFFFFFFFFu;
			glTextureSubImage2D(s_SSAOWhiteTex, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &white);
			glTextureParameteri(s_SSAOWhiteTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTextureParameteri(s_SSAOWhiteTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}

		// 全屏三角形 VBO/VAO（覆盖整个 NDC [-1,1] 的超大三角）
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

		// ---- RSM 资源 ----
		// 同时记录反射通量密度、世界位置、法线和深度。
		// 相邻像素可能来自不同物体；把它们的位置、法线插值混合，会算出不存在的反射点。
		// 因此每次取一个实际像素，避免边缘漏光或条纹。
		{
			FramebufferSpecification rsmSpec;
			rsmSpec.Width  = RSM_MAP_SIZE;
			rsmSpec.Height = RSM_MAP_SIZE;
			rsmSpec.Attachments = {
				FramebufferTextureFormat::RGBA16F,   // 0: Flux
				FramebufferTextureFormat::RGBA16F,   // 1: WorldPos
				FramebufferTextureFormat::RGBA16F,   // 2: Normal
				FramebufferTextureFormat::DEPTH32F   // depth (RSM Pass 自身的 z-test 用)
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

	// ----------
	// 全屏三角辅助：绑定 VAO 绘制 3 个顶点，覆盖整个 NDC
	// SSAO 与 RSM 使用同一套全屏三角。
	// ----------
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
		// 相机移动时仍寻找表面在前帧的位置。运行时没有相机身份，无法区分普通移动与同投影的切镜。
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
			dir = glm::mat3(tc.GetTransform()) * dl.Direction;
			if (glm::dot(dir, dir) < 1e-6f) continue;
			if (!dl.CastShadow) return false;
			dir = glm::normalize(dir);
			found = true;
			break;
		}
		if (!found) return false;

		// 沿光线方向看向原点；若光线接近竖直，改用 Z 轴确定画面的上方。
		glm::vec3 up = glm::abs(dir.y) > 0.95f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
		glm::vec3 lightPos = -dir * 30.0f; // 沿光线反向偏移 30 个世界单位
		glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), up);

		// 覆盖范围增大后，每个像素对应更大的表面，阴影细节会减少。
		const float half = s_ShadowSettings.OrthoHalfSize;
		glm::mat4 lightProj = glm::ortho(-half, half, -half, half, -50.0f, 50.0f);

		outMatrix   = lightProj * lightView;
		return true;
	}

	// ---- 阴影 Pass ----
	// 在提交队列之后、EndScene 之前重绘深度；完成后恢复调用方的 FBO 和视口。
	static void RunShadowPass(const glm::mat4& lightSpaceMatrix)
	{
		// --- 保存当前 FBO / viewport ---
		GLint  prevFBO = 0;
		GLint  prevViewport[4];
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);
		glGetIntegerv(GL_VIEWPORT, prevViewport);

		// --- 绑定 Shadow FBO ---
		s_ShadowFBO->Bind();                 // 内部已 glViewport 到 ShadowMap 尺寸
		glClear(GL_DEPTH_BUFFER_BIT);

		// 这次从光源方向绘制深度，提供光源投影矩阵；不读取已有阴影图。
		Renderer3D::SetShadowData(lightSpaceMatrix, glm::vec4(0.0f));

		// --- 深度预渲染 ---
		Renderer3D::ExecuteShadowPass(s_ShadowDepthShader);

		// --- 还原 FBO / viewport ---
		glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
	}

	// ---- RSM 光源 Pass ----
	// 从灯光方向重画几何，记录受光表面；即使关闭阴影，也要单独提供光源矩阵。
	// 画完后回到主画面的绘制目标，后续用这些表面估算反射光。
	static void RunRSMPass(const glm::mat4& lightSpaceMatrix)
	{
		if (!s_RSMFBO || !s_RSMDepthShader)
			return;

		GLint prevFBO = 0;
		GLint prevViewport[4];
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);
		glGetIntegerv(GL_VIEWPORT, prevViewport);

		s_RSMFBO->Bind(); // viewport → RSM_MAP_SIZE
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// RSM 使用自己的光源矩阵输入，不依赖 Shadow Pass 是否执行。
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

		// 创建 / 同步 FBO 尺寸
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
				s_RSMHistoryValid = false; // viewport 变化，历史失效
			}
		}
		if (!s_RSMIndirectFBO) return;

		// 提供这次的点数、半径和光照倍率；参数改变后不再混入按旧参数算出的颜色。
		const int numSamples = glm::clamp(s_RSMSettings.NumSamples, 0, (int)RSM_MAX_SAMPLES);
		const glm::vec4 header(float(numSamples), s_RSMSettings.Intensity,
		                       s_RSMSettings.SampleRadius, 2.0f * s_ShadowSettings.OrthoHalfSize);
		if (s_RSMParameters.Header != header)
		{
			s_RSMHistoryValid = false;
			s_RSMParameters.Header = header;
			// r²=(i+0.5)/N 让每个样本代表相同盘面积；减少 N 时仍覆盖整个采样半径。
			// 参数不变时复用这组点，每帧的旋转由 shader 完成。
			constexpr float GOLDEN_ANGLE = 2.39996322973f;
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

	// ---- 跨帧混合 ----
	// 把本帧间接光与上次结果混合以减轻噪点。两张结果图轮换，读旧图时只写另一张。
	static void RunRSMTemporalResolvePass(int w, int h,
	                                      const glm::mat4& viewProj,
	                                      GLint sceneDepthTex)
	{
		if (!s_RSMTemporalResolveShader || !s_RSMTemporalParamsUBO || !s_RSMIndirectFBO) return;

		// 两张历史图都使用当前视口尺寸。
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

		// 本帧只写 writeIdx、采样另一张历史，避免同一纹理同时作为输入和输出。
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
	// 同步读回当前 DRAW FBO 的原始深度，按当帧几何深度范围拉伸成 R8 灰度纹理。
	// 仅供调试，不做透视深度线性化。输入来自当前绑定目标及其视口。
	static void RunDepthVizPass()
	{
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		const int w = viewport[2], h = viewport[3];
		if (w <= 0 || h <= 0) return;

		// 只限制预览输出宽度；下面仍读取整个深度缓冲，glReadPixels 可能等待 GPU。
		int maxW = 512;
		int vw = std::min(w, maxW);
		int vh = std::max(1, h * vw / w);

		// 读回当前绑定 FBO 的深度（READ_FRAMEBUFFER）
		// 先确保 READ = DRAW
		GLint drawFBO = 0, readFBO = 0;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO);
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFBO);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, drawFBO);

		static std::vector<float> depthBuf;
		depthBuf.resize(size_t(w) * h);
		glReadPixels(0, 0, w, h, GL_DEPTH_COMPONENT, GL_FLOAT, depthBuf.data());

		// 先扫出当前帧的非空深度范围（忽略 d>=0.9999 的天空/远平面像素），
		// 然后把这个范围线性拉伸到 [0,1]。这样无论场景远近都能看清。
		float dMin = 1.0f, dMax = 0.0f;
		for (float d : depthBuf)
		{
			if (d >= 0.9999f) continue; // 天空/远平面
			if (d < dMin) dMin = d;
			if (d > dMax) dMax = d;
		}
		bool hasGeo = dMin <= dMax;
		float range = std::max(dMax - dMin, 0.000001f);

		// 按最近邻降采样，将 [dMin,dMax] 映射到 [0,1]，保持原始行方向。
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
					// 近黑远白，自动拉伸对比
					g = (d - dMin) / range;
				}
				if (g < 0.0f) g = 0.0f;
				if (g > 1.0f) g = 1.0f;
				pixels[size_t(y) * vw + x] = uint8_t(g * 255.0f);
			}
		}

		// 创建 / 重建 纹理
		if (s_DepthVizTex == 0 || int(s_DepthVizTexW) != vw || int(s_DepthVizTexH) != vh)
		{
			if (s_DepthVizTex) { glDeleteTextures(1, &s_DepthVizTex); s_DepthVizTex = 0; }
			glCreateTextures(GL_TEXTURE_2D, 1, &s_DepthVizTex);
			glTextureStorage2D(s_DepthVizTex, 1, GL_R8, vw, vh);
			glTextureParameteri(s_DepthVizTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTextureParameteri(s_DepthVizTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTextureParameteri(s_DepthVizTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTextureParameteri(s_DepthVizTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			// R8 单通道，ImGui 采样会只看 .r，通过 swizzle 把 .r 广播到 .rgb
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

		// 还原 READ FBO
		glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);

		// 更新预览纹理 ID
		s_LastPrepassDepthTex = s_DepthVizTex;
	}

	// ----------
	// RunSSAOPass — 在主 FBO 当前绑定时调用
	//
	// 前置条件：
	//   1. Z-Prepass 已完成：主 FBO 的 DEPTH + attachment2(view normal) 已写。
	//   2. 当前绑定的 FBO 必须是主 FBO（SceneRenderer 插入时机即是这里）。
	//
	// 流程：
	//   1. 从当前 FBO 查询 depth texture 和 normal texture (attachment2) 的 GL ID。
	//   2. 如视口变化则 Resize SSAO FBO。
	//   3. 上传 SSAOParams UBO（Projection / InvProjection / Radius / Kernel...）。
	//   4. 绑定 SSAO FBO，全屏三角形渲染，关深度测试。
	//   5. 还原主 FBO，把 SSAO 结果纹理绑到 slot 12 给主 Mesh shader 读取。
	// ----------
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

		// --- 查询主 FBO 的 depth / normal 附件纹理 ---
		GLint depthTex = 0, normalTex = 0;
		// 深度或深度模板纹理都通过 DEPTH_ATTACHMENT 查询。
		glGetNamedFramebufferAttachmentParameteriv(prevFBO,
			GL_DEPTH_ATTACHMENT,
			GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depthTex);
		glGetNamedFramebufferAttachmentParameteriv(prevFBO,
			GL_COLOR_ATTACHMENT2,
			GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &normalTex);
		if (depthTex == 0 || normalTex == 0)
			return; // FBO 没有必须的附件，跳过 SSAO

		s_LastViewNormalTex   = (uint32_t)normalTex;
		// 注意：不要把原始 DEPTH 附件覆盖到 s_LastPrepassDepthTex；
		// Pre-Z 预览用的是 RunDepthVizPass 生成的对比拉伸 R8 纹理。

		// --- 视口变化则同步 SSAO FBO 尺寸 ---
		const auto& spec = s_SSAOFBO->GetSpecifictaion();
		if (int(spec.Width) != w || int(spec.Height) != h)
			s_SSAOFBO->Resize(uint32_t(w), uint32_t(h));

		// --- 上传 SSAO UBO ---
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

		// --- 绑定 SSAO FBO，全屏渲染 ---
		s_SSAOFBO->Bind(); // 内部 glViewport
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

		// 登记到 Renderer3D 的 drawcall 日志 / 统计里（3 = 一个全屏三角形）
		Renderer3D::LogExternalDrawCall(
			Renderer3D::DrawCallInfo::PassType::SSAO, 3);

		glEnable(GL_DEPTH_TEST);

		// --- 还原主 FBO / viewport ---
		glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

		// 把 SSAO 结果纹理绑到 slot 12 供主 Mesh shader 读
		glBindTextureUnit(SSAO_MAP_SLOT, s_SSAOFBO->GetColorAttachmentRendererID(0));
	}

	// 关闭 SSAO 时绑 1×1 全白纹理到 slot 12，主 shader 无需分支
	static void BindSSAOWhiteFallback()
	{
		if (s_SSAOWhiteTex)
			glBindTextureUnit(SSAO_MAP_SLOT, s_SSAOWhiteTex);
	}

	// ---- 编辑器帧 ----
	// 顺序：2D → 提交3D/灯光 → Shadow/RSM → 深度法线 → SSAO/间接光累积 → 主3D → 天空/调试 → 后处理。
	// Scene 不归渲染器所有；模型首次加载会回填组件，Pass 资源与时域历史跨帧保留。
	void SceneRenderer::RenderScene(Scene& scene, const EditorCamera& camera)
	{
		UpdateRSMView(scene, camera.GetProjection(), &camera);
		entt::registry& registry = scene.GetRegistry();

		// 2D 颜色含透明度；上一帧后处理会关闭混合，这里重新设置。
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// 2D 渲染
		// 使用编辑器相机的 ViewProjection 初始化 2D 批处理
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

		// 后续阶段写入完整的几何和光照数据，不与旧附件内容混合。
		glDisable(GL_BLEND);

		// 灯光收集
		CollectAndUploadLights(scene);

		// 3D 渲染
		Renderer3D::BeginScene(camera);
		if (!s_IBLEnabled)
			Renderer3D::OverrideIBLFlag(false);

		// --- MeshRendererComponent ---
		// 单个 Mesh，优先使用 PBR Material，否则回退到 Color/Texture
		{
			auto view = registry.view<TransformComponent, MeshRendererComponent>();
			view.each([&](auto entity, TransformComponent& transform, MeshRendererComponent& mr)
			{
				if (mr.MeshData)
				{
					if (mr.Mat)
						Renderer3D::DrawMesh(mr.MeshData, transform.GetTransform(), mr.Mat, (int)entity);
					else if (mr.Texture)
						Renderer3D::DrawMesh(mr.MeshData, transform.GetTransform(), mr.Texture, (int)entity);
					else
						Renderer3D::DrawMesh(mr.MeshData, transform.GetTransform(), mr.Color, (int)entity);
				}
			});
		}

		// --- ModelRendererComponent ---
		// 多子网格模型，支持懒加载 + 覆盖材质
		{
			auto view = registry.view<TransformComponent, ModelRendererComponent>();
			view.each([&](auto entity, TransformComponent& transform, ModelRendererComponent& mr)
			{
				// 资产 ID 在导入或场景加载时登记。
				if (!mr.ModelAsset && mr.ModelID)
					mr.ModelAsset = AssetManager::Load<Model>(mr.ModelID);

				if (mr.ModelAsset)
					Renderer3D::DrawModel(mr.ModelAsset, transform.GetTransform(), mr.OverrideMaterials, mr.Color, (int)entity);
			});
		}

		// Shadow / RSM Pass
		// 必须在提交全部几何 (DrawMesh/DrawModel) 后、主 Pass EndScene 前。
		//
		// Shadow 与 RSM 共用光源方向，但各自决定是否执行；RSM 不受阴影开关控制。
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
			// 更新主 Pass 的阴影参数；关闭 Shadow 时 mode=0，保留已求出的光源矩阵。
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
			// 仅 shadow 开启时才绑 Shadow Map；关闭时主 shader mode==0 直接 return 1.0
			glBindTextureUnit(SHADOW_MAP_SLOT, s_ShadowFBO->GetDepthAttachmentRendererID());
		}

		// Z-Prepass
		// SSAO 和 RSMIndirect 都依赖 prepass 生成的深度和 view-space 法线。
		bool runPrepass = s_ZPrepassEnabled || s_SSAOEnabled || runRSM;
		if (runPrepass)
		{
			Renderer3D::ExecuteDepthPrepass(s_DepthPrepassShader);

			// 深度预览：读回深度并拉伸成 R8 灰度图，供 ImGui 显示。
			RunDepthVizPass();

			// 使用预写深度筛选主 Pass，LEQUAL 允许同一表面的等深度片元通过。
			// 深度写入在本帧结束后恢复。
			glDepthFunc(GL_LEQUAL);
			glDepthMask(GL_FALSE);
		}

		// SSAO Pass
		// 必须在 Z-Prepass 之后、主 Pass 之前。主 Mesh shader 会在 ambient
		// 合成点读 slot 12 的 AO。
		if (s_SSAOEnabled)
			RunSSAOPass(camera.GetProjection());
		else
			BindSSAOWhiteFallback();

		// 间接光与跨帧混合
		// 先取得预写的深度和法线，计算本帧反射光，再与前帧混合；主绘制随后读取结果。
		if (runRSM && runPrepass)
		{
			GLint mainFBO = 0;
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &mainFBO);
			GLint vp[4];
			glGetIntegerv(GL_VIEWPORT, vp);
			int w = vp[2], h = vp[3];

			// 查询主 FBO 附件
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

				// 还原主 FBO 和 viewport（两个新 pass 完成后需要回到主 FBO）
				glBindFramebuffer(GL_FRAMEBUFFER, mainFBO);
				glViewport(vp[0], vp[1], vp[2], vp[3]);
			}
		}
		else if (!runRSM)
		{
			// RSM 关闭时让 history 失效，下次开启时从第一帧重新累积
			s_RSMHistoryValid = false;
		}

		// 为主绘制选好间接光图；关闭时改用黑图。
		BindRSMResources();

		Renderer3D::EndScene();

		// 帧计数递增（在 EndScene 之后，确保同帧渲染一致）
		++s_FrameIndex;

		if (runPrepass)
		{
			glDepthMask(GL_TRUE);
			glDepthFunc(GL_LESS);
		}

		// 天空盒
		// 放在不透明几何之后、UI/调试线之前
		if (s_IBLEnabled)
			IBLPrecompute::RenderSkybox(camera.GetViewMatrix(), camera.GetProjection());

		// 剪裁可视化调试
		// 用 Renderer2D 线段绘制所有提交物体的世界空间 AABB：
		//   绿色 = 通过剪裁；红色 = 被剪掉
		if (Renderer3D::GetDebugCullingVizEnabled())
		{
			Renderer2D::BeginScene(camera);
			Renderer3D::DrawDebugAABBs();
			Renderer2D::EndScene();
		}

		// 后处理在全部 HDR 绘制结束后执行。
		s_PostProcess->Render(s_MainSceneColorTex, s_MainSceneWidth, s_MainSceneHeight);
	}

	// ----------
	// ---- 运行时帧 ----
	// 与编辑器使用相同 Pass 顺序，view 由相机世界变换求逆得到。
	// ----------
	void SceneRenderer::RenderRuntime(Scene& scene, const glm::mat4& projection, const glm::mat4& cameraTransform)
	{
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

		// 后续阶段写入完整的几何和光照数据，不与旧附件内容混合。
		glDisable(GL_BLEND);

		// 灯光收集
		CollectAndUploadLights(scene);

		// 3D 渲染
		glm::mat4 viewProj3D = projection * glm::inverse(cameraTransform);
		glm::vec3 cameraPos  = glm::vec3(cameraTransform[3]); // 从变换矩阵提取位移列
		Renderer3D::BeginScene(viewProj3D, cameraPos, glm::inverse(cameraTransform));
		if (!s_IBLEnabled)
			Renderer3D::OverrideIBLFlag(false);

		{
			auto view = registry.view<TransformComponent, MeshRendererComponent>();
			view.each([&](auto entity, TransformComponent& transform, MeshRendererComponent& mr)
			{
				if (mr.MeshData)
				{
					if (mr.Mat)
						Renderer3D::DrawMesh(mr.MeshData, transform.GetTransform(), mr.Mat, (int)entity);
					else if (mr.Texture)
						Renderer3D::DrawMesh(mr.MeshData, transform.GetTransform(), mr.Texture, (int)entity);
					else
						Renderer3D::DrawMesh(mr.MeshData, transform.GetTransform(), mr.Color, (int)entity);
				}
			});
		}

		{
			auto view = registry.view<TransformComponent, ModelRendererComponent>();
			view.each([&](auto entity, TransformComponent& transform, ModelRendererComponent& mr)
			{
				if (!mr.ModelAsset && mr.ModelID)
					mr.ModelAsset = AssetManager::Load<Model>(mr.ModelID);

				if (mr.ModelAsset)
					Renderer3D::DrawModel(mr.ModelAsset, transform.GetTransform(), mr.OverrideMaterials, mr.Color, (int)entity);
			});
		}

		// Shadow / RSM Pass（Runtime）
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

		// Z-Prepass
		// SSAO 和 RSMIndirect 都依赖 prepass 生成的深度和 view-space 法线。
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

			// SSAO Pass
			if (s_SSAOEnabled)
				RunSSAOPass(projection);
			else
				BindSSAOWhiteFallback();

			// RSM Indirect + Temporal Resolve
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

			// RSM 绑定
			BindRSMResources();

			Renderer3D::EndScene();
			++s_FrameIndex;

			if (runPrepass)
			{
				glDepthMask(GL_TRUE);
				glDepthFunc(GL_LESS);
			}
		}

		// 天空盒
		if (s_IBLEnabled)
		{
			glm::mat4 viewMat3D = glm::inverse(cameraTransform);
			IBLPrecompute::RenderSkybox(viewMat3D, projection);
		}

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

	// ----------
	// CollectAndUploadLights — 遍历场景灯光实体，填充 Light UBO
	//
	// UBO 内存布局 (std140, binding = 3)：
	//   offset 0  : GPULightUBO header (16 bytes)
	//               - int NumDirectionalLights
	//               - int NumPointLights
	//               - int _pad[2]
	//   offset 16 : GPULightData[0..MAX_DIR-1] 方向光数组
	//   offset 16 + MAX_DIR*64 : GPULightData[0..MAX_POINT-1] 点光数组
	//
	// 每帧在 3D 渲染开始前调用，确保 shader 读到最新灯光。
	// ----------
	void SceneRenderer::CollectAndUploadLights(Scene& scene)
	{
		entt::registry& registry = scene.GetRegistry();

		GPULightUBO header{};
		GPULightData dirLights[MAX_DIRECTIONAL_LIGHTS]{};
		GPULightData pointLights[MAX_POINT_LIGHTS]{};

		// --- 收集方向光 ---
		{
			auto view = registry.view<TransformComponent, DirectionalLightComponent>();
			view.each([&](auto entity, TransformComponent& tc, DirectionalLightComponent& dl)
			{
				if (header.NumDirectionalLights >= (int)MAX_DIRECTIONAL_LIGHTS) return;
				auto& light = dirLights[header.NumDirectionalLights];
				glm::vec3 dir = glm::mat3(tc.GetTransform()) * dl.Direction;
				if (glm::dot(dir, dir) < 1e-6f) return;
				dir = glm::normalize(dir);
				light.Direction = glm::vec4(dir, 0.0f);           // w=0 表示方向光
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

		// --- 收集点光源 ---
		{
			auto view = registry.view<TransformComponent, PointLightComponent>();
			view.each([&](auto entity, TransformComponent& tc, PointLightComponent& pl)
			{
				if (header.NumPointLights >= (int)MAX_POINT_LIGHTS) return;
				auto& light = pointLights[header.NumPointLights];
				light.Direction = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // w=1 表示点光
				light.Color     = glm::vec4(pl.Color, pl.Intensity);
				light.Position  = glm::vec4(tc.Translation, pl.Radius);
				light.CastShadow = pl.CastShadow ? 1 : 0;
				header.NumPointLights++;
			});
		}

		// --- 上传到 GPU ---
		uint32_t offset = 0;

		// header
		s_LightUBO->SetData(&header, sizeof(GPULightUBO), offset);
		offset += sizeof(GPULightUBO);

		// 方向光数组
		if (header.NumDirectionalLights > 0)
			s_LightUBO->SetData(dirLights, header.NumDirectionalLights * sizeof(GPULightData), offset);
		offset += MAX_DIRECTIONAL_LIGHTS * sizeof(GPULightData);

		// 点光数组
		if (header.NumPointLights > 0)
			s_LightUBO->SetData(pointLights, header.NumPointLights * sizeof(GPULightData), offset);
	}

} // namespace Redflag
