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

	// =================================================================
	// 静态资源：Light UBO
	// =================================================================
	static Ref<UniformBuffer> s_LightUBO;

	// =================================================================
	// Shadow Pass 资源
	//
	// 当前实现：单方向光 + 单张 2048×2048 DEPTH32F Shadow Map。
	// 覆盖范围由固定正交视锥控制（见 BuildLightSpaceMatrix），
	// 对"沙盒/展示"尺度场景够用；后续可接入场景包围盒或 CSM。
	// =================================================================
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

	// =================================================================
	// SSAO 资源
	//
	// 策略 C：Z-Prepass 顺带把 view-space 法线写到主 FBO attachment2
	//   （RGBA16F）。SSAO Pass 再采样主 FBO 的深度 + 法线 RT，做半球采
	//   样输出 AO 到一张单独的 RGBA8 FBO。主 Mesh shader 在 ambient
	//   合成处乘上 AO。为保持简单，没有额外的模糊 Pass —— 直接把 64
	//   个样本的结果输出，接受一点噪点。
	// =================================================================
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
	// Z-Prepass 深度可视化纹理 id（RGBA8，线性化后），留给 ImGui 面板预览
	static uint32_t          s_LastPrepassDepthTex  = 0;

	// 深度可视化 pass 资源：CPU 读回 depth → 线性化 → 上传到 RGBA8 纹理
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

	// 后处理拥有独立的输出与绘制资源。
	static Scope<PostProcess> s_PostProcess;
	static uint32_t s_MainSceneColorTex = 0;
	static int s_MainSceneWidth = 0;
	static int s_MainSceneHeight = 0;

	BloomSettings& SceneRenderer::GetBloomSettings() { return s_PostProcess->GetBloomSettings(); }
	ToneMapSettings& SceneRenderer::GetToneMapSettings() { return s_PostProcess->GetToneMapSettings(); }

	// RSM：512 × 512 的 VPL 容器。
	static constexpr uint32_t RSM_MAP_SIZE        = 512;
	static constexpr uint32_t RSM_FLUX_SLOT       = 13;
	static constexpr uint32_t RSM_WORLDPOS_SLOT   = 14;
	static constexpr uint32_t RSM_NORMAL_SLOT     = 15;
	static constexpr uint32_t RSM_DEPTH_SLOT      = 16;
	static constexpr uint32_t RSM_UBO_BINDING     = 6;   // 已用：0~5
	static constexpr uint32_t RSM_MAX_SAMPLES     = 128;

	// RSM Params UBO 布局（std140）：
	//   vec4 Header  : x = numSamples, y = intensity, z = sampleRadius, w = enabled
	//   vec4 Samples[RSM_MAX_SAMPLES] : xy = 单位圆盘偏移, z = 样本权重, w = unused
	struct GPURSMParams
	{
		glm::vec4 Header;
		glm::vec4 Samples[RSM_MAX_SAMPLES];
	};

	static RSMSettings        s_RSMSettings;
	static Ref<Framebuffer>   s_RSMFBO;
	static Ref<Shader>        s_RSMDepthShader;
	static Ref<UniformBuffer> s_RSMParamsUBO;
	static glm::vec4          s_RSMSamples[RSM_MAX_SAMPLES]{};

	// 关闭 RSM 时绑到 slot 13/14/15/17 的 1×1 黑纹理
	static uint32_t           s_RSMBlackTex = 0;

	// ---- RSM TAA 资源 ----
	static constexpr uint32_t RSM_INDIRECT_SCENE_DEPTH_SLOT = 18;
	static constexpr uint32_t RSM_INDIRECT_VIEW_NORMAL_SLOT = 19;
	static constexpr uint32_t RSM_RESOLVED_SLOT             = 17;

	static Ref<Framebuffer>   s_RSMIndirectFBO;       // 屏幕分辨率 RGBA16F：本帧 RSM 计算结果
	static Ref<Framebuffer>   s_RSMHistoryFBO[2];     // ping-pong 历史，各屏幕分辨率 RGBA16F
	static int                s_RSMHistoryIndex = 0;  // 当前写入目标索引（0 或 1）
	static uint32_t           s_FrameIndex      = 0;  // 全局帧计数，驱动旋转 jitter
	static glm::mat4          s_PrevViewProj    = glm::mat4(1.0f);
	static bool               s_RSMHistoryValid = false; // 历史 buffer 是否有有效内容
	static float              s_RSMBlendAlpha   = 0.9f;  // 时域融合权重

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
		// RSM FBO 自身的深度附件（正交投影下线性 = 离光源 near 平面的距离）
		return s_RSMFBO ? s_RSMFBO->GetDepthAttachmentRendererID() : 0;
	}
	uint32_t SceneRenderer::GetRSMResolvedTextureID()
	{
		// 上一帧写入完成的 resolved buffer（历史 ping-pong 中另一侧）
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

	void SceneRenderer::Init()
	{
		s_LightUBO = UniformBuffer::Create(LIGHT_UBO_SIZE, LIGHT_UBO_BINDING);

		// IBL 预计算：不自动加载，等用户在 Draw Calls 面板点击 "Load HDR..." 手动选择。
		// （自动加载会在文件不存在时打印错误，且无法运行时切换环境贴图）
		IBLPrecompute::InitEmpty();

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
		// 3 颜色 MRT + 1 深度，OpenGLFramebuffer 自动调用 glDrawBuffers。
		// 注意 RSM 的 Flux / WorldPos / Normal 本质上是 G-buffer 数据，不是可平滑插值
		// 的最终颜色；如果对它们做 LINEAR，会把不同表面混成一个"假 VPL"，直接
		// 表现成漏光和沿几何边缘的结构条纹。因此这里创建后显式改成 NEAREST。
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

		// 预生成 128 个低差异单位圆盘采样点。
		// 纯随机极坐标 + 每像素旋转很容易在平坦表面上形成粉末状白噪声；
		// Vogel disk 能更均匀地覆盖圆盘，shader 端再按有效样本数归一化，
		// 让 NumSamples 主要影响平滑度而不是整体亮度。
		{
			constexpr float GOLDEN_ANGLE = 2.39996322973f;
			for (uint32_t i = 0; i < RSM_MAX_SAMPLES; ++i)
			{
				float t      = (static_cast<float>(i) + 0.5f) / static_cast<float>(RSM_MAX_SAMPLES);
				float radius = glm::sqrt(t);
				float theta  = GOLDEN_ANGLE * static_cast<float>(i);
				s_RSMSamples[i] = glm::vec4(
					radius * glm::cos(theta),
					radius * glm::sin(theta),
					1.0f,
					0.0f);
			}
		}

		// 1×1 黑纹理：RSM 关闭时绑到 13/14/15 槽位
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
		s_FrameIndex      = 0;
		if (s_RSMBlackTex) { glDeleteTextures(1, &s_RSMBlackTex); s_RSMBlackTex = 0; }
		IBLPrecompute::Shutdown();
	}

	// ==================================================================
	// 全屏三角辅助：绑定 VAO 绘制 3 个顶点，覆盖整个 NDC
	// SSAO 与 RSM 使用同一套全屏三角。
	// ==================================================================
	static void DrawFullscreenTriangle()
	{
		glBindVertexArray(s_SSAOQuadVAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
	}

	// ==================================================================
	// TryBuildLightSpaceMatrix — 从场景里找到第一盏开启 CastShadow 的方向光
	//
	// 成功：outLight 记录光源方向（世界空间，已归一化），
	//       outMatrix 填入 Ortho × View，返回 true。
	// 失败：返回 false，调用方应当跳过 Shadow Pass。
	//
	// 当前实现选择固定视锥：以原点为中心的 40×40×100 正交盒，
	// near/far = -50/50，可完整覆盖 [-25,25] 的主要摆放区域。
	// 以后接入场景包围盒或级联阴影（CSM）再替换这个函数即可。
	// ==================================================================
	static bool TryBuildLightSpaceMatrix(Scene& scene,
		glm::vec3& outLightDir, glm::mat4& outMatrix)
	{
		entt::registry& registry = scene.GetRegistry();

		bool found = false;
		glm::vec3 dir(0.0f);

		auto view = registry.view<TransformComponent, DirectionalLightComponent>();
		for (auto e : view)
		{
			auto& tc = view.get<TransformComponent>(e);
			auto& dl = view.get<DirectionalLightComponent>(e);
			if (!dl.CastShadow) continue;
			dir = glm::normalize(glm::mat3(tc.GetTransform()) * dl.Direction);
			// 防御：全零方向会让 lookAt 退化
			if (glm::dot(dir, dir) < 1e-6f) continue;
			found = true;
			break;
		}
		if (!found) return false;

		// 构造光源 view：从 "光源所在虚拟位置" 看向原点（沿光线前进方向）
		// up 取 (0,1,0)；若光几乎平行 Y 轴，用 (0,0,1) 规避退化
		glm::vec3 up = glm::abs(dir.y) > 0.95f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
		glm::vec3 lightPos = -dir * 30.0f; // 退后 30 单位，保证 far=50 时场景在视锥内
		glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), up);

		// 覆盖范围：由 ImGui 调整（增大半宽会让 shadow map 精度下降）
		const float half = s_ShadowSettings.OrthoHalfSize;
		glm::mat4 lightProj = glm::ortho(-half, half, -half, half, -50.0f, 50.0f);

		outLightDir = dir;
		outMatrix   = lightProj * lightView;
		return true;
	}

	// ==================================================================
	// RunShadowPass — 把当前队列里的几何画进 Shadow Map
	//
	// 调用时机：在 Renderer3D 完成"提交"但尚未 EndScene 之前。
	// 流程：
	//   1. 保存现在绑定的 draw framebuffer + viewport，pass 后还原。
	//   2. 绑定 Shadow FBO，清深度，使用光源空间矩阵做深度预渲染。
	//   3. 正面剔除 (optional)：部分消除 shadow acne；对薄片几何会让
	//      正面丢阴影，这里选择保留 BACK 剔除，依赖 bias + PCF。
	// ==================================================================
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

		// 先把 LightSpaceMatrix 写入 Camera UBO，ShadowDepth.glsl 从这里读
		// （ShadowParams 也顺便设成 disable，确保 Shadow Pass 自身不触发阴影采样）
		Renderer3D::SetShadowData(lightSpaceMatrix, glm::vec4(0.0f));

		// --- 深度预渲染 ---
		Renderer3D::ExecuteShadowPass(s_ShadowDepthShader);

		// --- 还原 FBO / viewport ---
		glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
	}

	// ==================================================================
	// RunRSMPass — 把场景从光源视角再渲染一次，输出 Flux/WorldPos/Normal
	// 三张 MRT 颜色附件到 s_RSMFBO，主 Pass 之后随机采样这些 VPL 算间接光。
	//
	// 与 RunShadowPass 的区别：
	//   - 目标 FBO 不同（独立的 s_RSMFBO，不影响 s_ShadowFBO）
	//   - 用 s_RSMDepthShader（写 3 个颜色附件）而不是空 fragment 的 shadow 深度 shader
	//   - 调用 ExecuteRSMPass 而不是 ExecuteShadowPass（多上传 Material UBO）
	//
	// 注意：会复用 Camera UBO 中由 RunShadowPass 写入的 LightSpaceMatrix，
	//   所以本函数必须在 RunShadowPass 之后、Renderer3D::SetShadowData
	//   被主 Pass 阴影参数覆盖之前调用。当前调用顺序确保了这一点。
	// ==================================================================
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

		// 写 LightSpaceMatrix（如果上一步 RunShadowPass 没跑，这里也兜底写入）
		Renderer3D::SetShadowData(lightSpaceMatrix, glm::vec4(0.0f));

		// 用 RSM shader 重绘几何（带 Material UBO + BaseColorMap）
		Renderer3D::ExecuteRSMPass(s_RSMDepthShader);

		glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
	}

	// ==================================================================
	// RunRSMIndirectPass — 屏幕空间 RSM 间接光延迟 pass
	//
	// 从 Z-Prepass 的深度 + view-normal 重建每像素世界坐标和法线，
	// 计算 RSM 间接光并写入 s_RSMIndirectFBO（屏幕分辨率 RGBA16F）。
	// ==================================================================
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

		// 上传 RSM Params UBO（RSMIndirect shader 读取）
		const int numSamples = glm::clamp(s_RSMSettings.NumSamples, 0, (int)RSM_MAX_SAMPLES);
		GPURSMParams params{};
		params.Header = glm::vec4(float(numSamples), s_RSMSettings.Intensity,
		                          s_RSMSettings.SampleRadius, 1.0f);
		for (uint32_t i = 0; i < RSM_MAX_SAMPLES; ++i)
			params.Samples[i] = s_RSMSamples[i];
		s_RSMParamsUBO->SetData(&params, sizeof(GPURSMParams));

		// 绑定 RSM map 纹理（VPL 数据）
		glBindTextureUnit(RSM_FLUX_SLOT,     s_RSMFBO->GetColorAttachmentRendererID(0));
		glBindTextureUnit(RSM_WORLDPOS_SLOT, s_RSMFBO->GetColorAttachmentRendererID(1));
		glBindTextureUnit(RSM_NORMAL_SLOT,   s_RSMFBO->GetColorAttachmentRendererID(2));
		glBindTextureUnit(RSM_DEPTH_SLOT,    s_RSMFBO->GetDepthAttachmentRendererID());
		// 主 FBO 深度和 view-normal（接收点重建用）
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

	// ==================================================================
	// RunRSMTemporalResolvePass — 时域累积 RSMIndirect 结果
	//
	// 将本帧 s_RSMIndirectFBO 与历史 ping-pong buffer 融合，
	// 输出到 s_RSMHistoryFBO[s_RSMHistoryIndex]，然后翻转索引。
	// ==================================================================
	static void RunRSMTemporalResolvePass(int w, int h,
	                                      const glm::mat4& viewProj,
	                                      GLint sceneDepthTex)
	{
		if (!s_RSMTemporalResolveShader || !s_RSMTemporalParamsUBO || !s_RSMIndirectFBO) return;

		// 创建 / 同步 history FBO 尺寸
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

		// slot 0 = 本帧 RSMIndirect
		glBindTextureUnit(0, s_RSMIndirectFBO->GetColorAttachmentRendererID(0));
		// slot 1 = 上一帧 resolved history
		glBindTextureUnit(1, s_RSMHistoryFBO[readIdx]->GetColorAttachmentRendererID(0));
		// slot 2 = 场景深度（反投影用）
		glBindTextureUnit(2, (GLuint)sceneDepthTex);

		GPURSMTemporalParams tParams{};
		tParams.InvViewProj  = glm::inverse(viewProj);
		tParams.PrevViewProj = s_PrevViewProj;
		tParams.BlendAlpha   = s_RSMBlendAlpha;
		tParams.ResetHistory = s_RSMHistoryValid ? 0 : 1;
		s_RSMTemporalParamsUBO->SetData(&tParams, sizeof(tParams));

		DrawFullscreenTriangle();

		glEnable(GL_DEPTH_TEST);

		// 翻转 ping-pong 索引，更新历史矩阵
		s_RSMHistoryIndex = 1 - s_RSMHistoryIndex;
		s_PrevViewProj    = viewProj;
		s_RSMHistoryValid = true;
	}

	// ==================================================================
	// BindRSMResources — 在主 Pass 之前绑 RSM resolved 纹理到 slot 17
	//
	// 关闭时绑 1×1 黑纹理，Lo += 0 × albedo = 无贡献。
	// ==================================================================
	static void BindRSMResources()
	{
		// 主 Pass 需要的 resolved 纹理
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

	// ==================================================================
	// RunDepthVizPass — 当前绑定 FBO 的 depth 读回 CPU，线性化后上传到
	// RGBA8 纹理供 ImGui 预览。比直接用 sampler 采 DEPTH24STENCIL8 更可靠。
	// ==================================================================
	static void RunDepthVizPass(uint32_t /*depthTex*/, int w, int h, float nearP, float farP)
	{
		if (w <= 0 || h <= 0) return;

		// 为了不阻塞 GPU，限制回读分辨率（最大 512 宽）
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
		bool hasGeo = dMax > dMin;
		float range = hasGeo ? (dMax - dMin) : 1.0f;

		// 降采样 + 翻转 Y + 归一化到 [dMin, dMax]
		(void)nearP; (void)farP;
		static std::vector<uint8_t> pixels;
		pixels.resize(size_t(vw) * vh);
		for (int y = 0; y < vh; ++y)
		{
			int srcY = y * h / vh;
			if (srcY < 0) srcY = 0;
			if (srcY >= h) srcY = h - 1;
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
			// R8 单通道，ImGui 采样会只看 .r——通过 swizzle 把 .r 广播到 .rgb
			GLint swizzle[4] = { GL_RED, GL_RED, GL_RED, GL_ONE };
			glTextureParameteriv(s_DepthVizTex, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
			s_DepthVizTexW = (uint32_t)vw;
			s_DepthVizTexH = (uint32_t)vh;
		}

		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTextureSubImage2D(s_DepthVizTex, 0, 0, 0, vw, vh, GL_RED, GL_UNSIGNED_BYTE, pixels.data());

		// 还原 READ FBO
		glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);

		// 更新预览纹理 ID
		s_LastPrepassDepthTex = s_DepthVizTex;
	}

	// ==================================================================
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
	// ==================================================================
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
		// 主 FBO 深度可能是 DEPTH_STENCIL 或纯 DEPTH，两种都查
		glGetNamedFramebufferAttachmentParameteriv(prevFBO,
			GL_DEPTH_STENCIL_ATTACHMENT,
			GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depthTex);
		if (depthTex == 0)
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
		// Pre-Z 预览用的是 RunDepthVizPass 生成的线性化 R8 纹理。

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

	// ==================================================================
	// RenderScene — 编辑器模式渲染
	//
	// 逻辑完全等价于原来 Scene::OnUpdateEditor 中的渲染部分。
	// 只是调用方从 Scene 内部变成了 SceneRenderer。
	//
	// 步骤：
	//   1. Renderer2D 画所有 2D Sprite 和 Circle
	//   2. Renderer3D 画所有 3D Mesh 和 Model
	//
	// 为什么 registry 要用引用：
	//   SceneRenderer 不拥有 Scene，也不复制 registry。
	//   它只在一帧内读取数据、发出绘制命令，帧结束后不持有任何状态。
	// ==================================================================
	void SceneRenderer::RenderScene(Scene& scene, const EditorCamera& camera)
	{
		entt::registry& registry = scene.GetRegistry();
		const bool hasMainHDRTarget =
			s_MainSceneColorTex != 0 && s_MainSceneWidth > 0 && s_MainSceneHeight > 0;

		// ======================== 2D 渲染 ========================
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

		// ======================== 灯光收集 ========================
		CollectAndUploadLights(scene);

		// ======================== 3D 渲染 ========================
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

		// ======================== Shadow / RSM Pass ========================
		// 必须在提交全部几何 (DrawMesh/DrawModel) 后、主 Pass EndScene 前。
		//
		// 关键：RSM **不能** 被 s_ShadowEnabled 关掉。
		// 主 shader 用 v_LightSpacePos.xy 当作 RSM 采样 UV，所以只要场景里有
		// 方向光，就必须把 LightSpaceMatrix 上传到 Camera UBO；否则 UV 退化为
		// identity，RSM 直接采到错位/未定义数据。Shadow 和 RSM 各自独立判断。
		glm::vec3 lightDir;
		glm::mat4 lightSpaceMatrix(1.0f);
		bool hasDirLight = TryBuildLightSpaceMatrix(scene, lightDir, lightSpaceMatrix);
		bool runShadow   = s_ShadowEnabled && hasDirLight;
		bool runRSM      = s_RSMSettings.Enabled && hasDirLight;

		if (runShadow)
			RunShadowPass(lightSpaceMatrix);

		if (runRSM)
			RunRSMPass(lightSpaceMatrix);

		if (hasDirLight)
		{
			// 必须无条件上传 LightSpaceMatrix —— RSM 在主 shader 里也依赖它来
			// 计算采样 UV。Shadow 关闭时把 mode 写 0，shader 跳过 shadow map 查询，
			// 但 LightSpaceMatrix 仍然有效。
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

		// ======================== Z-Prepass ========================
		// SSAO 和 RSMIndirect 都依赖 prepass 生成的深度和 view-space 法线。
		bool runPrepass = s_ZPrepassEnabled || s_SSAOEnabled || runRSM;
		if (runPrepass)
		{
			Renderer3D::ExecuteDepthPrepass(s_DepthPrepassShader);

			// 深度可视化：线性化后写入 RGBA8 FBO 供 ImGui 预览
			{
				GLint curFBO = 0, dt = 0;
				glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &curFBO);
				glGetNamedFramebufferAttachmentParameteriv(curFBO, GL_DEPTH_STENCIL_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &dt);
				if (dt == 0)
					glGetNamedFramebufferAttachmentParameteriv(curFBO, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &dt);
				GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
				RunDepthVizPass((uint32_t)dt, vp[2], vp[3], camera.GetNearClip(), camera.GetFarClip());
			}

			// Prepass 已将深度写满；主 Pass 只测试深度不再写入，
			// 避免两次浮点计算的微小差异导致 Z-fighting
			glDepthFunc(GL_LEQUAL);
			glDepthMask(GL_FALSE);
		}

		// ======================== SSAO Pass ========================
		// 必须在 Z-Prepass 之后、主 Pass 之前。主 Mesh shader 会在 ambient
		// 合成点读 slot 12 的 AO。
		if (s_SSAOEnabled)
			RunSSAOPass(camera.GetProjection());
		else
			BindSSAOWhiteFallback();

		// ======================== RSM Indirect + Temporal Resolve ========================
		// 从当前绑定的主 FBO 查询深度和 view-normal 纹理，运行延迟 RSM 屏幕空间 pass
		// 和时域累积 pass，将结果写入 ping-pong history buffer。
		// 必须在 Z-Prepass 之后（有 depth + view-normal），在主 Pass 之前（写 resolved）。
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
				GL_DEPTH_STENCIL_ATTACHMENT,
				GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depthTex);
			if (depthTex == 0)
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

		// ======================== RSM 绑定 ========================
		// 把 TAA resolved 结果绑到 slot 17，主 shader 采样。
		// 关闭时绑黑纹理，间接光贡献为 0。
		BindRSMResources();

		Renderer3D::EndScene();

		// 帧计数递增（在 EndScene 之后，确保同帧渲染一致）
		++s_FrameIndex;

		if (runPrepass)
		{
			glDepthMask(GL_TRUE);
			glDepthFunc(GL_LESS);
		}

		// ======================== 天空盒 ========================
		// 放在不透明几何之后、UI/调试线之前
		if (s_IBLEnabled)
			IBLPrecompute::RenderSkybox(camera.GetViewMatrix(), camera.GetProjection());

		// ======================== 剪裁可视化调试 ========================
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

	// ==================================================================
	// RenderRuntime — 运行时模式渲染
	// ==================================================================
	void SceneRenderer::RenderRuntime(Scene& scene, const glm::mat4& projection, const glm::mat4& cameraTransform)
	{
		entt::registry& registry = scene.GetRegistry();
		const bool hasMainHDRTarget =
			s_MainSceneColorTex != 0 && s_MainSceneWidth > 0 && s_MainSceneHeight > 0;

		// ======================== 2D 渲染 ========================
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

		// ======================== 2D 粒子渲染 ========================
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

		// ======================== 灯光收集 ========================
		CollectAndUploadLights(scene);

		// ======================== 3D 渲染 ========================
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

		// ======================== Shadow / RSM Pass（Runtime）========================
		glm::vec3 lightDir_rt;
		glm::mat4 lightSpaceMatrix_rt(1.0f);
		bool hasDirLight_rt = TryBuildLightSpaceMatrix(scene, lightDir_rt, lightSpaceMatrix_rt);
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

		// ======================== Z-Prepass ========================
		// SSAO 和 RSMIndirect 都依赖 prepass 生成的深度和 view-space 法线。
		{
			bool runPrepass = s_ZPrepassEnabled || s_SSAOEnabled || runRSM_rt;
			if (runPrepass)
			{
				Renderer3D::ExecuteDepthPrepass(s_DepthPrepassShader);

				// 深度可视化
				{
					GLint curFBO = 0, dt = 0;
					glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &curFBO);
					glGetNamedFramebufferAttachmentParameteriv(curFBO, GL_DEPTH_STENCIL_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &dt);
					if (dt == 0)
						glGetNamedFramebufferAttachmentParameteriv(curFBO, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &dt);
					float n = projection[3][2] / (projection[2][2] - 1.0f);
					float f = projection[3][2] / (projection[2][2] + 1.0f);
					GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
					RunDepthVizPass((uint32_t)dt, vp[2], vp[3], n, f);
				}

				glDepthFunc(GL_LEQUAL);
				glDepthMask(GL_FALSE);
			}

			// ======================== SSAO Pass ========================
			if (s_SSAOEnabled)
				RunSSAOPass(projection);
			else
				BindSSAOWhiteFallback();

			// ======================== RSM Indirect + Temporal Resolve ========================
			if (runRSM_rt && runPrepass)
			{
				GLint mainFBO = 0;
				glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &mainFBO);
				GLint vp[4];
				glGetIntegerv(GL_VIEWPORT, vp);
				int w = vp[2], h = vp[3];

				GLint depthTex = 0, normalTex = 0;
				glGetNamedFramebufferAttachmentParameteriv(mainFBO,
					GL_DEPTH_STENCIL_ATTACHMENT,
					GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depthTex);
				if (depthTex == 0)
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

			// ======================== RSM 绑定 ========================
			BindRSMResources();

			Renderer3D::EndScene();
			++s_FrameIndex;

			if (runPrepass)
			{
				glDepthMask(GL_TRUE);
				glDepthFunc(GL_LESS);
			}
		}

		// ======================== 天空盒 ========================
		if (s_IBLEnabled)
		{
			glm::mat4 viewMat3D = glm::inverse(cameraTransform);
			IBLPrecompute::RenderSkybox(viewMat3D, projection);
		}

		// ======================== 剪裁可视化调试 ========================
		if (Renderer3D::GetDebugCullingVizEnabled())
		{
			Renderer2D::BeginScene(projection, cameraTransform);
			Renderer3D::DrawDebugAABBs();
			Renderer2D::EndScene();
		}

		// 后处理在全部 HDR 绘制结束后执行。
		s_PostProcess->Render(s_MainSceneColorTex, s_MainSceneWidth, s_MainSceneHeight);
	}

	// ==================================================================
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
	// ==================================================================
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
				glm::vec3 dir = glm::normalize(glm::mat3(tc.GetTransform()) * dl.Direction);
				light.Direction = glm::vec4(dir, 0.0f);           // w=0 表示方向光
				light.Color     = glm::vec4(dl.Color, dl.Intensity);
				light.Position  = glm::vec4(0.0f);                // 方向光无位置
				light.CastShadow = dl.CastShadow ? 1 : 0;
				header.NumDirectionalLights++;
			});
		}

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
