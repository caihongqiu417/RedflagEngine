#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/EditorCamera.h"
#include "Redflag/Renderer/UniformBuffer.h"
#include "Redflag/Renderer/PostProcess.h"

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
		// TAA resolved 间接光纹理（供 Render Passes 面板预览）
		static uint32_t GetRSMResolvedTextureID();
		// TAA 历史混合权重（0=每帧独立, 0.9=强时域平滑），供 ImGui 调节
		static float& GetRSMBlendAlpha();

		// 返回后处理的 LDR 输出纹理。
		static uint32_t GetFinalColorTextureID();

	private:
		// 遍历 registry 收集灯光数据并上传 Light UBO
		static void CollectAndUploadLights(Scene& scene);
	};

} // namespace Redflag
