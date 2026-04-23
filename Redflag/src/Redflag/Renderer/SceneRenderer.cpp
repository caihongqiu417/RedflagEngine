// =====================================================================
// SceneRenderer.cpp — 场景渲染调度器
//
// 职责：
//   1. 每帧收集灯光数据 → 上传 Light UBO (binding = 3)
//   2. 遍历 registry → 调用 Renderer2D / Renderer3D 发出绘制命令
//   3. 后续扩展：Shadow Pass、PostProcess 等多 pass 编排
// =====================================================================

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

#include "Redflag/Core/AssetManager.h"
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

	// =================================================================
	// Bloom 资源
	//
	// 算法概述：
	//   1. Brightness Extract：从主 HDR buffer 提取亮度 > threshold 的片元
	//      输出到 s_BloomMips[0]（半分辨率）
	//
	//   2. Downsample chain：用 glBlitFramebuffer GL_LINEAR 纯硬件双线性下采样
	//      s_BloomMips[0] → [1] → [2] → … → [N-1]
	//      优点：零 shader 开销，带宽极低
	//
	//   3. Upsample chain：波松圆盘 12 点采样上采样，写入上一层（GL_ONE/GL_ONE 叠加）
	//      s_BloomMips[N-1] → 叠加到 [N-2] → … → [0]
	//      写目标 Mips[i-1]、读源 Mips[i]，不同对象，无读写同源问题
	//
	//   4. Composite：合并 HDR 原图 + s_BloomMips[0]，进行 ACES tonemap + gamma
	//      输出到 s_PostFBO（RGBA8 LDR）
	// =================================================================
	static constexpr int    MAX_BLOOM_MIPS = 6;

	// Bloom 金字塔：每层尺寸为 w>>(i+1) × h>>(i+1)
	// 第 0 层 = 半分辨率（亮度提取层），每多一层尺对半分
	static Ref<Framebuffer>  s_BloomMips[MAX_BLOOM_MIPS];

	// 后处理最终输出 FBO：存储 ACES tonemap + gamma 完成的 LDR 颜色
	// 不能直接写回主 FBO attachment0（同 FBO 读写不合法）
	static Ref<Framebuffer>  s_PostFBO;

	static Ref<Shader>       s_BloomBrightShader;    // 亮度提取：输出 lum > threshold 的片元
	static Ref<Shader>       s_BloomUpsampleShader;   // 波松圆盘 12-tap 上采样
	static Ref<Shader>       s_PostCompositeShader;   // HDR + bloom → ACES + gamma → LDR
	static uint32_t          s_BlackFallbackTex = 0;  // Bloom 关闭/未就绪时绑定的 1x1 黑纹理
	static bool              s_PostColorValid   = false; // 只有 Composite pass 成功执行后才允许 Viewport 使用
	static uint32_t          s_MainSceneColorTex = 0;    // ForgeLayer 显式登记的主场景 HDR 颜色纹理
	static int               s_MainSceneWidth    = 0;
	static int               s_MainSceneHeight   = 0;

	// Bloom 参数 UBO（binding = 5）
	// shaderc 用 Vulkan 语义编译 GLSL，块外 naked non-opaque uniform（如
	// `uniform float u_Threshold;`）会被拒绝；必须包进 uniform block。
	// u_BloomA.x = Threshold, y = Intensity, z = Radius, w = unused
	// u_BloomB.xy = TexelSize,           zw = unused
	static constexpr uint32_t BLOOM_UBO_BINDING = 5;
	struct GPUBloomParams { glm::vec4 A; glm::vec4 B; };
	static Ref<UniformBuffer> s_BloomParamsUBO;

	static BloomSettings     s_BloomSettings;

	BloomSettings& SceneRenderer::GetBloomSettings() { return s_BloomSettings; }
	uint32_t SceneRenderer::GetBloomTextureID()
	{
		return s_BloomMips[0] ? s_BloomMips[0]->GetColorAttachmentRendererID(0) : 0;
	}
	void SceneRenderer::SetMainSceneColorTarget(uint32_t colorTex, int width, int height)
	{
		s_MainSceneColorTex = colorTex;
		s_MainSceneWidth = width;
		s_MainSceneHeight = height;
	}
	uint32_t SceneRenderer::GetFinalColorTextureID()
	{
		// s_PostFBO 在 Init 时就会创建，但真正可显示要等 Composite pass 至少成功写过一次。
		// 否则 ImGui 会显示一张未定义内容的纹理，常见表现就是整屏白块。
		return (s_PostFBO && s_PostColorValid) ? s_PostFBO->GetColorAttachmentRendererID(0) : 0;
	}

	// CPU 侧灯光缓冲区，每帧填充后一次性上传
	// 布局：[GPULightUBO header (16B)] + [LightData[] 数组]
	static constexpr uint32_t LIGHT_UBO_SIZE =
		sizeof(GPULightUBO) +
		(MAX_DIRECTIONAL_LIGHTS + MAX_POINT_LIGHTS) * sizeof(GPULightData);

	void SceneRenderer::Init()
	{
		s_LightUBO = UniformBuffer::Create(LIGHT_UBO_SIZE, LIGHT_UBO_BINDING);

		// IBL 预计算：加载 HDR 环境贴图，生成 Irradiance / Prefilter / BRDF LUT
		// 只在启动时执行一次（约 1~2 秒）。HDR 文件不存在时会打印错误并跳过，
		// 此时 shader 会回退到常数环境光，不会影响程序运行。
		IBLPrecompute::Init("assets/textures/environment.hdr");

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

		// Bloom / Composite 的黑色回退纹理。
		// 不再把 sampler2D 绑定到纹理 0，因为 core profile 下采样未绑定纹理是未定义行为。
		{
			glCreateTextures(GL_TEXTURE_2D, 1, &s_BlackFallbackTex);
			glTextureStorage2D(s_BlackFallbackTex, 1, GL_RGBA8, 1, 1);
			uint32_t black = 0x00000000u;
			glTextureSubImage2D(s_BlackFallbackTex, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &black);
			glTextureParameteri(s_BlackFallbackTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTextureParameteri(s_BlackFallbackTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
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

		// ---- Bloom 资源 ----
		//
		// 初始尺寸不重要，每帧在 RunBloomPass 内按当前 viewport 动态 Resize。
		// 这里只预分配对象。
		{
			FramebufferSpecification bloomSpec;
			bloomSpec.Width  = 640;
			bloomSpec.Height = 360;
			bloomSpec.Attachments = { FramebufferTextureFormat::RGBA16F };

			for (int i = 0; i < MAX_BLOOM_MIPS; ++i)
				s_BloomMips[i] = Framebuffer::Create(bloomSpec);
		}
		{
			// s_PostFBO: 存储 ACES + gamma 完成的 LDR 输出，供 ForgeLayer viewport 显示。
			// 格式用 RGBA8（LDR 足够）。
			FramebufferSpecification postSpec;
			postSpec.Width  = 1280;
			postSpec.Height = 720;
			postSpec.Attachments = { FramebufferTextureFormat::RGBA8 };
			s_PostFBO = Framebuffer::Create(postSpec);
		}

		// 亮度提取 shader：输入 HDR，输出超过 threshold 的高光片元
		s_BloomBrightShader   = Shader::Create("assets/shaders/Bloom_Brightness.glsl");
		// 泊松圆盘 12-tap 上采样 shader：用于 Bloom 上采样叠加链
		s_BloomUpsampleShader = Shader::Create("assets/shaders/Bloom_Upsample.glsl");
		// Composite shader：合并 HDR 原图 + Bloom，执行 ACES tonemap + gamma
		s_PostCompositeShader = Shader::Create("assets/shaders/PostProcess_Composite.glsl");
		s_PostColorValid = false;

		// Bloom 参数 UBO
		s_BloomParamsUBO = UniformBuffer::Create(sizeof(GPUBloomParams), BLOOM_UBO_BINDING);
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
		if (s_BlackFallbackTex) { glDeleteTextures(1, &s_BlackFallbackTex); s_BlackFallbackTex = 0; }
		if (s_SSAOQuadVBO)  { glDeleteBuffers (1, &s_SSAOQuadVBO);  s_SSAOQuadVBO  = 0; }
		if (s_SSAOQuadVAO)  { glDeleteVertexArrays(1, &s_SSAOQuadVAO); s_SSAOQuadVAO = 0; }
		// Bloom 资源
		for (int i = 0; i < MAX_BLOOM_MIPS; ++i)
			s_BloomMips[i].reset();
		s_PostFBO.reset();
		s_BloomBrightShader.reset();
		s_BloomUpsampleShader.reset();
		s_PostCompositeShader.reset();
		s_BloomParamsUBO.reset();
		s_PostColorValid = false;
		s_MainSceneColorTex = 0;
		s_MainSceneWidth = 0;
		s_MainSceneHeight = 0;
		IBLPrecompute::Shutdown();
	}

	void SceneRenderer::BeginScene()
	{
	}

	void SceneRenderer::EndScene()
	{
	}

	// ==================================================================
	// 全屏三角辅助：绑定 VAO 绘制 3 个顶点，覆盖整个 NDC
	// s_SSAOQuadVAO 复用给 Bloom pass（节省一个 VAO 对象）
	// ==================================================================
	static void DrawFullscreenTriangle()
	{
		glBindVertexArray(s_SSAOQuadVAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
	}

	// ==================================================================
	// RunBloomPass — 提取高光 + 下采样金字塔 + 上采样叠加
	//
	// 参数：
	//   hdrTex — 主 FBO attachment0 的 GL 纹理 ID（RGBA16F HDR 原图）
	//   w, h   — 主 viewport 当前像素尺寸
	//
	// 流程：
	//   1. Brightness Extract（1 draw）：
	//      把 hdrTex 中亮度超过 threshold 的片元写入 s_BloomMips[0]
	//      （尺寸 = w/2 × h/2，半分辨率）。
	//
	//   2. Downsample chain（MipLevels-1 次 blit）：
	//      用 glBlitFramebuffer GL_LINEAR 把 [i-1] 层 blit 到 [i] 层，
	//      每层尺寸减半。blit 由 GPU 硬件执行双线性滤波，无需 shader。
	//
	//   3. Upsample chain（MipLevels-1 次 draw + additive blend）：
	//      从最小层 [N-1] 逐层往上，用泊松圆盘 12-tap 上采样，
	//      以 GL_ONE/GL_ONE 加法叠加到 [i-1] 层。
	//      读源 = s_BloomMips[i]，写目标 = s_BloomMips[i-1]，
	//      两者是不同的 Framebuffer 对象，不存在读写同源问题。
	//
	// 调用结束后：s_BloomMips[0] 包含多频段叠加的 bloom 结果。
	// ==================================================================
	static void RunBloomPass(uint32_t hdrTex, int w, int h)
	{
		if (!s_BloomBrightShader || !s_SSAOQuadVAO)
			return;

		const int effectiveMips = std::min(s_BloomSettings.MipLevels, MAX_BLOOM_MIPS);
		if (effectiveMips < 1) return;

		// 先把本帧会用到的所有 Bloom mip 都 Resize 到正确尺寸并清黑。
		// 这样即使后续某一步 blit / draw 状态异常，也不会采到上一帧残留的亮斑。
		for (int i = 0; i < effectiveMips; ++i)
		{
			int mipW = std::max(w >> (i + 1), 1);
			int mipH = std::max(h >> (i + 1), 1);
			const auto& spec = s_BloomMips[i]->GetSpecifictaion();
			if ((int)spec.Width != mipW || (int)spec.Height != mipH)
				s_BloomMips[i]->Resize((uint32_t)mipW, (uint32_t)mipH);

			s_BloomMips[i]->Bind();
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			glClear(GL_COLOR_BUFFER_BIT);
		}

		// ---- Step 1: Brightness Extract ----
		// 目标尺寸 = 主 viewport 的一半（最小 1×1）
		int mip0W = std::max(w / 2, 1);
		int mip0H = std::max(h / 2, 1);

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);

		// 绑定 MipLevel 0 FBO，渲染亮度提取结果
		s_BloomMips[0]->Bind();
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		s_BloomBrightShader->Bind();

		// Bloom 参数：threshold 放到 BloomA.x，这一 pass 其他字段不用
		{
			float bloomThreshold = std::max(s_BloomSettings.Threshold, 0.5f);
			GPUBloomParams params{};
			params.A = glm::vec4(bloomThreshold, 0.0f, 0.0f, 0.0f);
			params.B = glm::vec4(0.0f);
			if (s_BloomParamsUBO)
				s_BloomParamsUBO->SetData(&params, sizeof(params));
		}

		// 绑定 HDR 纹理到 slot 0
		glBindTextureUnit(0, hdrTex);
		DrawFullscreenTriangle();
		Renderer3D::LogExternalDrawCall(Renderer3D::DrawCallInfo::PassType::BloomBright, 3);

		// ---- Step 2: Downsample chain ----
		// glBlitFramebuffer GL_LINEAR 把 mip[i] blit 到 mip[i+1]，每层尺寸减半。
		for (int i = 0; i + 1 < effectiveMips; ++i)
		{
			const auto& srcSpec = s_BloomMips[i]->GetSpecifictaion();
			const auto& dstSpec = s_BloomMips[i + 1]->GetSpecifictaion();

			glBindFramebuffer(GL_READ_FRAMEBUFFER, s_BloomMips[i]->GetFramebufferID());
			glReadBuffer(GL_COLOR_ATTACHMENT0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_BloomMips[i + 1]->GetFramebufferID());
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
			glBlitFramebuffer(
				0, 0, (int)srcSpec.Width, (int)srcSpec.Height,
				0, 0, (int)dstSpec.Width, (int)dstSpec.Height,
				GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}

		// ---- Step 3: Upsample chain ----
		// 从最小层 [N-1] 逐层往上，用泊松圆盘 12-tap 加法叠加到 [i-1] 层。
		if (s_BloomUpsampleShader)
		{
			s_BloomUpsampleShader->Bind();
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE);  // 加法叠加

			for (int i = effectiveMips - 1; i >= 1; --i)
			{
				// 读源：s_BloomMips[i]（更低分辨率）
				// 写目标：s_BloomMips[i-1]（更高分辨率）
				const auto& srcSpec = s_BloomMips[i]->GetSpecifictaion();

				// 上传该层对应的 TexelSize 和 Radius 到 UBO
				float radius = std::max(s_BloomSettings.Radius, 0.1f);
				GPUBloomParams params{};
				params.A = glm::vec4(s_BloomSettings.Threshold, s_BloomSettings.Intensity, radius, 0.0f);
				params.B = glm::vec4(1.0f / (float)srcSpec.Width, 1.0f / (float)srcSpec.Height, 0.0f, 0.0f);
				if (s_BloomParamsUBO) s_BloomParamsUBO->SetData(&params, sizeof(params));

				// 绑定源纹理到 slot 0
				glBindTextureUnit(0, s_BloomMips[i]->GetColorAttachmentRendererID(0));

				// 绑定目标 FBO
				s_BloomMips[i - 1]->Bind();
				glDrawBuffer(GL_COLOR_ATTACHMENT0);

				DrawFullscreenTriangle();
				Renderer3D::LogExternalDrawCall(Renderer3D::DrawCallInfo::PassType::BloomUp, 3);
			}

			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // 恢复默认混合
			glDisable(GL_BLEND);
		}

		glEnable(GL_DEPTH_TEST);
	}

	// ==================================================================
	// RunCompositePass — 合并 HDR 原图 + Bloom，执行 ACES tonemap + gamma
	//
	// 参数：
	//   hdrTex — 主 FBO attachment0 的 GL 纹理 ID（RGBA16F HDR 原图）
	//   w, h   — 主 viewport 当前像素尺寸
	//
	// 输出：s_PostFBO（RGBA8 LDR），ForgeLayer 从中读取显示纹理。
	//
	// 注意：即使 Bloom 关闭也会执行此 pass，因为 tonemap 是必须的
	//       （主 FBO 现在是 HDR 格式，不能直接显示）。
	// ==================================================================
	static void RunCompositePass(uint32_t hdrTex, int w, int h)
	{
		if (!s_PostCompositeShader || !s_PostFBO || !s_SSAOQuadVAO)
		{
			s_PostColorValid = false;
			return;
		}

		// 按需 Resize s_PostFBO 与 viewport 同尺寸
		{
			const auto& spec = s_PostFBO->GetSpecifictaion();
			if ((int)spec.Width != w || (int)spec.Height != h)
			{
				s_PostColorValid = false;
				s_PostFBO->Resize((uint32_t)w, (uint32_t)h);
			}
		}

		s_PostFBO->Bind();
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_DEPTH_TEST);

		s_PostCompositeShader->Bind();

		// slot 0 = HDR 原图（主 FBO attachment0，RGBA16F）
		glBindTextureUnit(0, hdrTex);

		// slot 1 = Bloom 结果（s_BloomMips[0]，包含多频段叠加）
		// 若 Bloom 关闭或结果未就绪，则绑定 1x1 黑纹理，避免采样未绑定纹理的未定义行为。
		uint32_t bloomTex = (s_BloomSettings.Enabled && s_BloomMips[0])
			? s_BloomMips[0]->GetColorAttachmentRendererID(0)
			: s_BlackFallbackTex;
		glBindTextureUnit(1, bloomTex);

		// Composite 中直接对半分辨率 bloom mip0 做局部模糊采样，绕开不稳定的金字塔回放大链。
		float bloomTexelX = 1.0f;
		float bloomTexelY = 1.0f;
		if (s_BloomSettings.Enabled && s_BloomMips[0])
		{
			const auto& bloomSpec = s_BloomMips[0]->GetSpecifictaion();
			if (bloomSpec.Width > 0 && bloomSpec.Height > 0)
			{
				bloomTexelX = 1.0f / (float)bloomSpec.Width;
				bloomTexelY = 1.0f / (float)bloomSpec.Height;
			}
		}
		float bloomRadius    = glm::clamp(s_BloomSettings.Radius, 0.1f, 4.0f);
		float bloomIntensity = glm::clamp(s_BloomSettings.Intensity, 0.0f, 1.0f);

		GPUBloomParams params{};
		params.A = glm::vec4(s_BloomSettings.Threshold, bloomIntensity, bloomRadius, 0.0f);
		params.B = glm::vec4(bloomTexelX, bloomTexelY, 0.0f, 0.0f);
		if (s_BloomParamsUBO)
			s_BloomParamsUBO->SetData(&params, sizeof(params));

		DrawFullscreenTriangle();
		Renderer3D::LogExternalDrawCall(Renderer3D::DrawCallInfo::PassType::Composite, 3);
		s_PostColorValid = true;

		glEnable(GL_DEPTH_TEST);
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
				// 懒加载：路径有效但 ModelAsset 未加载时，通过 AssetManager 加载
				if (!mr.ModelAsset && !mr.FilePath.empty())
					mr.ModelAsset = AssetManager::GetOrLoad<Model>(mr.FilePath);

				if (mr.ModelAsset)
					Renderer3D::DrawModel(mr.ModelAsset, transform.GetTransform(), mr.OverrideMaterials, mr.Color, (int)entity);
			});
		}

		// ======================== Shadow Pass ========================
		// 必须在提交全部几何 (DrawMesh/DrawModel) 后、主 Pass EndScene 前。
		// 找到主方向光 → 深度预渲染 → 把阴影参数 + Shadow Map 提供给主 shader
		glm::vec3 lightDir;
		glm::mat4 lightSpaceMatrix(1.0f);
		bool hasShadow = s_ShadowEnabled && TryBuildLightSpaceMatrix(scene, lightDir, lightSpaceMatrix);
		if (hasShadow)
		{
			RunShadowPass(lightSpaceMatrix);

			// 把光源空间矩阵 + 阴影参数正式写进 Camera UBO，供主 Mesh shader
			// 在片元阶段做 PCF / PCSS 采样。
			glm::vec4 shadowParams(
				s_ShadowSettings.DepthBias,
				s_ShadowSettings.NormalBias,
				1.0f / float(SHADOW_MAP_SIZE),
				float(s_ShadowSettings.Mode));
			glm::vec4 shadowParams2(
				s_ShadowSettings.PcfRadius,
				s_ShadowSettings.LightSize,
				0.0f, 0.0f);
			Renderer3D::SetShadowData(lightSpaceMatrix, shadowParams, shadowParams2);

			// 绑定 Shadow Map 深度纹理到 slot 11（与主 shader binding = 11 对应）
			glBindTextureUnit(SHADOW_MAP_SLOT, s_ShadowFBO->GetDepthAttachmentRendererID());
		}

		// ======================== Z-Prepass ========================
		// SSAO 依赖 prepass 生成的 view-space 法线，因此 SSAO 开启时必须
		// 强制跑 Z-Prepass；否则尊重用户开关。
		bool runPrepass = s_ZPrepassEnabled || s_SSAOEnabled;
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

		Renderer3D::EndScene();

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

		// ======================== Bloom + Composite 后处理 ========================
		// 必须在所有几何、天空盒、调试覆盖层全部完成后进行，
		// 确保吸收了全帧的 HDR 内容。
		{
			if (hasMainHDRTarget)
			{
				// Bloom pass：提取高光 → blit 下采样 → 波松叠加上采样
				if (s_BloomSettings.Enabled)
					RunBloomPass(s_MainSceneColorTex, s_MainSceneWidth, s_MainSceneHeight);

				// Composite pass：HDR + bloom → ACES tonemap + gamma → s_PostFBO
				// 即使 Bloom 关闭，也必须进行 tonemap（主 FBO 是 HDR 格式）
				RunCompositePass(s_MainSceneColorTex, s_MainSceneWidth, s_MainSceneHeight);
			}
			else
			{
				s_PostColorValid = false;
			}
		}
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
				if (!mr.ModelAsset && !mr.FilePath.empty())
					mr.ModelAsset = AssetManager::GetOrLoad<Model>(mr.FilePath);

				if (mr.ModelAsset)
					Renderer3D::DrawModel(mr.ModelAsset, transform.GetTransform(), mr.OverrideMaterials, mr.Color, (int)entity);
			});
		}

		// ======================== Shadow Pass（Runtime）========================
		if (s_ShadowEnabled)
		{
			glm::vec3 lightDir;
			glm::mat4 lightSpaceMatrix(1.0f);
			if (TryBuildLightSpaceMatrix(scene, lightDir, lightSpaceMatrix))
			{
				RunShadowPass(lightSpaceMatrix);
				glm::vec4 shadowParams(
					s_ShadowSettings.DepthBias,
					s_ShadowSettings.NormalBias,
					1.0f / float(SHADOW_MAP_SIZE),
					float(s_ShadowSettings.Mode));
				glm::vec4 shadowParams2(
					s_ShadowSettings.PcfRadius,
					s_ShadowSettings.LightSize,
					0.0f, 0.0f);
				Renderer3D::SetShadowData(lightSpaceMatrix, shadowParams, shadowParams2);
				glBindTextureUnit(SHADOW_MAP_SLOT, s_ShadowFBO->GetDepthAttachmentRendererID());
			}
		}

		// ======================== Z-Prepass ========================
		// SSAO 需要 prepass 的法线输出；开启 SSAO 时强制跑 prepass
		bool runPrepass = s_ZPrepassEnabled || s_SSAOEnabled;
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
				// Runtime 路径：从投影矩阵提取 near/far
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

		Renderer3D::EndScene();

		if (runPrepass)
		{
			glDepthMask(GL_TRUE);
			glDepthFunc(GL_LESS);
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

		// ======================== Bloom + Composite 后处理 ========================
		{
			if (hasMainHDRTarget)
			{
				if (s_BloomSettings.Enabled)
					RunBloomPass(s_MainSceneColorTex, s_MainSceneWidth, s_MainSceneHeight);
				RunCompositePass(s_MainSceneColorTex, s_MainSceneWidth, s_MainSceneHeight);
			}
			else
			{
				s_PostColorValid = false;
			}
		}
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
