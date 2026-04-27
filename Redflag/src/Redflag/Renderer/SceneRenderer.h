#pragma once

// =====================================================================
// SceneRenderer — 场景渲染调度器（第一版：纯转发）
//
// 设计思路：
//   以前 Scene::OnUpdateEditor / OnUpdateRuntime 里既收集实体又直接
//   调用 Renderer2D / Renderer3D 发 draw。这会导致「场景数据层」和
//   「帧级渲染编排」耦合在一起，后续加 Shadow Pass、Deferred、
//   PostProcess 等多 pass 时无处可插。
//
//   SceneRenderer 就是专门负责「这一帧怎么画」的中间层，把渲染编排
//   逻辑从 Scene 中剥离出来。Scene 只负责回答「有哪些东西」，
//   SceneRenderer 负责回答「这些东西这一帧应该怎样被画出来」。
//
// 第一版做法（纯转发 / pass-through）：
//   1. Scene 把 registry 暴露出来。
//   2. SceneRenderer 遍历 registry 收集 Renderable，直接调用
//      Renderer2D / Renderer3D 完成绘制。
//   3. 视觉输出和改动前完全一致，验证通过后再逐步拆分 pass。
//
// 后续演进方向：
//   - 增加 RenderScene（帧级数据快照）做收集-排序-执行的三阶段
//   - 增加 ShadowPass / MainPass / PostProcessPass / OverlayPass
//   - 支持 HDR framebuffer + PBR shader
// =====================================================================

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/EditorCamera.h"
#include "Redflag/Renderer/UniformBuffer.h"

#include <glm/glm.hpp>

namespace Redflag {

	// 前向声明；SceneRenderer 只需要指针 / 引用访问 Scene
	class Scene;

	// =================================================================
	// GPU 灯光数据布局 (std140)
	//
	// 每个灯光占 64 字节，与 shader 中的 LightData 一一对应。
	// Type 编码：0 = 方向光，1 = 点光源
	// =================================================================
	struct GPULightData
	{
		glm::vec4 Direction;   // xyz = 归一化方向, w = type (0=dir, 1=point)
		glm::vec4 Color;       // xyz = 颜色,       w = 强度
		glm::vec4 Position;    // xyz = 世界坐标,   w = 半径（点光）
		int       CastShadow;  // 0 或 1
		int       _pad[3];     // std140 对齐到 16 字节
	};

	// UBO 头部：灯光数量 + 对齐填充
	struct GPULightUBO
	{
		int NumDirectionalLights;
		int NumPointLights;
		int _pad[2];
		// 后面紧跟 LightData[] 数组，通过 offset 写入
	};

	static constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 1;
	static constexpr uint32_t MAX_POINT_LIGHTS = 16;
	static constexpr uint32_t LIGHT_UBO_BINDING = 3;

	// =================================================================
	// Shadow 调参结构（对 ImGui 暴露）
	//
	// 所有字段在每帧 Shadow Pass 之前被读取，运行时修改会立刻生效。
	// Mode: 0=Off, 1=PCF 3×3, 2=PCSS
	// =================================================================
	struct ShadowSettings
	{
		int   Mode        = 1;       // 0=Off, 1=PCF, 2=PCSS
		float DepthBias   = 0.0015f; // NDC 深度偏移，消除 shadow acne
		float NormalBias  = 1.0f;    // 纹素单位，薄面片额外防自阴影
		float PcfRadius   = 1.5f;    // PCF 模式下的核半径（纹素）
		float LightSize   = 6.0f;    // PCSS 模式下的虚拟光源尺寸（纹素）
		float OrthoHalfSize = 5.0f;  // 光源正交视锥半宽（世界空间）。Cornell box 等小场景用 4~5；
		                             // 大场景需要在 UI 里调到 25~50。半宽过大会让 RSM 大部分 texel
		                             // 落在场景外（flux=0），间接光信号被稀释，bleeding 看不出来。
	};

	// =================================================================
	// SSAO 调参（ImGui 暴露）
	// =================================================================
	struct SSAOSettings
	{
		float Radius = 0.5f;    // view space 半径（米）
		float Bias   = 0.025f;  // 深度比较 bias，防止自遮蔽
		float Power  = 2.0f;    // 对比度增强指数
		int   KernelSize = 64;  // 采样数（上限 64，shader 内固定数组）
	};
	// =================================================================
	// Bloom 调参（ImGui 暴露）
	//
	// 算法：亮度提取 → blit 下采样金字塔 → 波松圆盘上采样叠加 → Composite
	// =================================================================
	struct BloomSettings
	{
		bool  Enabled    = true;   // 全局开关
		float Threshold  = 0.8f;   // 亮度阈值：超过此值的片元才进入 Bloom（0.8 = 较明显，2.0 = 只有极亮处）
		float Intensity  = 0.6f;   // Bloom 叠加强度（乘法系数）
		float Radius     = 2.0f;   // 泊松圆盘采样半径（控制上采样层的散布大小）
		int   MipLevels  = 6;      // 下采样层数（越多层 = 越大的散布形状；2~6）
	};
	// =================================================================
	// Tonemap 调参
	// =================================================================
	struct ToneMapSettings
	{
		float Exposure = 1.0f;  // 曝光度倍率（乘在 tonemap 前的 HDR 上）
	};

	// =================================================================
	// RSM (Reflective Shadow Maps) 调参
	//
	// 在 Shadow Pass 之外额外渲染一遍场景到 RSM FBO（Flux/WorldPos/Normal
	// 三张 RGBA16F 颜色附件），主 Mesh shader 在直接光累加之后随机采样
	// 这些 VPL 计算 1-bounce 间接漫反射。
	//
	// Enabled = false 时本特性完全不参与渲染，等价于关闭前的状态。
	// =================================================================
	struct RSMSettings
	{
		bool  Enabled      = true;    // 关闭时跳过 RSM Pass + shader 中 NumSamples 置 0
		int   NumSamples   = 128;     // 每像素采样的 VPL 数量（16~128，越大越平滑）
		// shader 中按 ÷NumSamples 归一化（form-factor 平均值），单样本 geom ~0.01-0.05，
		// 默认 ~10 才能让 Cornell box 里的 color bleeding 肉眼可见。
		float Intensity    = 10.0f;   // 间接光整体强度（0~50）
		// 在 shadow map UV 空间的采样半径。OrthoHalfSize=5 时 1.0 UV ≈ 10 世界单位，
		// Cornell box 占 ~0.6 UV，0.4 足以让样本采到对面墙体产生 color bleeding。
		float SampleRadius = 0.40f;   // 0.01 ~ 0.6
	};
	class SceneRenderer
	{
	public:
		// 初始化 / 销毁渲染资源（Light UBO 等）
		static void Init();
		static void Shutdown();

		static void BeginScene();
		static void EndScene();

		static void RenderScene(Scene& scene, const EditorCamera& camera);
		static void RenderRuntime(Scene& scene, const glm::mat4& projection, const glm::mat4& cameraTransform);

		// 供编辑器 UI 读写阴影调参
		static ShadowSettings& GetShadowSettings();
		static SSAOSettings&   GetSSAOSettings();
		static RSMSettings&    GetRSMSettings();

		// IBL / Shadow / Z-Prepass / SSAO 全局开关（菜单栏 Toggle）
		static bool& GetIBLEnabled();
		static bool& GetShadowEnabled();
		static bool& GetZPrepassEnabled();
		static bool& GetSSAOEnabled();

		// SSAO 纹理（RGBA8 R 通道 = AO 可见度）GL 纹理 ID，供 ImGui 面板预览
		static uint32_t GetSSAOTextureID();
		// 主 FBO 的 view-space 法线附件 GL 纹理 ID（Z-Prepass 写入）
		static uint32_t GetViewNormalTextureID();
		// Shadow Map 深度纹理 ID，供 Shadow 面板预览
		static uint32_t GetShadowMapTextureID();
		// Z-Prepass 深度纹理 ID，供 Shadow 面板预览
		static uint32_t GetPrepassDepthTextureID();

		// 供编辑器 UI 读写 Bloom 调参
		static BloomSettings&   GetBloomSettings();
		static ToneMapSettings& GetToneMapSettings();
		static uint32_t GetBloomTextureID();
		static void SetMainSceneColorTarget(uint32_t colorTex, int width, int height);

		// RSM 三张产物纹理（供 Render Passes 面板预览）：
		//   Flux      = 颜色附件 0，光源照亮表面的出射通量
		//   Normal    = 颜色附件 2，世界法线（压缩到 [0,1]）
		//   Distance  = 深度附件，光源视角下到 near 平面的距离（正交投影下线性）
		static uint32_t GetRSMFluxTextureID();
		static uint32_t GetRSMNormalTextureID();
		static uint32_t GetRSMDistanceTextureID();

		// 返回后处理最终输出纹理 ID（s_PostFBO 的 RGBA8 attachment）
		// ForgeLayer viewport 应调用此函数而非直接读主 FBO attachment0（主 FBO 已改为 RGBA16F HDR）
		static uint32_t GetFinalColorTextureID();

	private:
		// 遍历 registry 收集灯光数据并上传 Light UBO
		static void CollectAndUploadLights(Scene& scene);
	};

} // namespace Redflag
