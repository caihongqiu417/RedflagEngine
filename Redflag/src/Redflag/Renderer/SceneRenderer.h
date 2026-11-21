#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/EditorCamera.h"
#include "Redflag/Renderer/UniformBuffer.h"
#include "Redflag/Renderer/PostProcess.h"

#include <glm/glm.hpp>

namespace Redflag {

	// 前向声明；SceneRenderer 只需要指针 / 引用访问 Scene
	class Scene;

	// ----------
	// 送给着色器的灯光数据。std140 规定字段的间隔，使 C++ 与着色器按相同位置读取。
	// 每盏灯占 64 字节，与着色器里的 LightData 对应。
	// ----------
	struct GPULightData
	{
		glm::vec4 Direction;   // xyz = 归一化方向, w = type (0=dir, 1=point)
		glm::vec4 Color;       // xyz = 颜色,       w = 强度
		glm::vec4 Position;    // xyz = 世界坐标,   w = 半径（点光）
		int       CastShadow;  // 0 或 1
		int       _pad[3];     // 补足末尾 16 字节，不能当成下一盏灯的数据
	};

	// 灯光数量放在整块数据的开头，着色器据此决定读取几盏灯。
	struct GPULightUBO
	{
		int NumDirectionalLights;
		int NumPointLights;
		int _pad[2];
		// 后面依次存放方向光和点光源数组。
	};

	static constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 1;
	static constexpr uint32_t MAX_POINT_LIGHTS = 16;
	static constexpr uint32_t LIGHT_UBO_BINDING = 3;

	// ----------
	// Shadow 调参结构（对 ImGui 暴露）
	//
	// 所有字段在每帧 Shadow Pass 之前被读取，运行时修改会立刻生效。
	// Mode: 0=Off, 1=PCF 3×3, 2=PCSS
	// ----------
	struct ShadowSettings
	{
		int   Mode        = 1;       // 0=Off, 1=PCF, 2=PCSS
		float DepthBias   = 0.0015f; // [0,1] 深度比较偏移，减轻表面自阴影
		float NormalBias  = 1.0f;    // 纹素单位，薄面片额外防自阴影
		float PcfRadius   = 1.5f;    // PCF 模式下的核半径（纹素）
		float LightSize   = 6.0f;    // PCSS 模式下的虚拟光源尺寸（纹素）
		float OrthoHalfSize = 5.0f;  // 光源正交视锥半宽（世界空间）。Cornell box 等小场景用 4~5；
		                             // 大场景可调大覆盖范围，但固定分辨率下每个纹素会覆盖更大面积。
	};

	// ----------
	// SSAO 调参（ImGui 暴露）
	// ----------
	struct SSAOSettings
	{
		float Radius = 0.5f;    // 观察空间采样半径，与场景长度单位一致
		float Bias   = 0.025f;  // 观察空间深度偏移，与 Radius 同单位
		float Power  = 2.0f;    // 对比度增强指数
		int   KernelSize = 64;  // 采样数（上限 64，shader 内固定数组）
	};
	// ---- RSM 参数 ----
	// 反射阴影图（RSM）记录灯光照到的表面，用这些表面估算一次反弹的光。
	// 把本帧与前帧结果混合以减轻噪点，再加到主画面；关闭时改读黑图，贡献为零。
	struct RSMSettings
	{
		bool  Enabled      = true;    // 关闭后使历史失效，下次启用从当前帧开始累积
		int   NumSamples   = 128;     // 每个像素选取的反射点数量（16~128，越大噪点越少）
		float Intensity    = 1.0f;    // 间接光倍率；1 保留采样面积与漫反射公式算出的值
		// 以整张光源图的宽度为 1。半宽为 5 时，半径 0.4 对应 4 个世界单位。
		float SampleRadius = 0.40f;   // 0.01 ~ 0.6
	};
	/*
	 * 组织一帧场景画面：读取实体和灯光，安排阴影、遮蔽、间接光、主画面与后处理的先后顺序。
	 * Scene 由调用方持有；当前渲染目标、间接光的历史画面及开关仍是单视口静态状态。
	 * 编辑器绑定主场景帧缓冲并借入颜色纹理，PostProcess 持有后处理的最终输出。
	 */
	class SceneRenderer
	{
	public:
		// ---- 初始化与销毁 ----
		// Init/Shutdown 必须在有效图形上下文中调用。
		static void Init();
		static void Shutdown();


		// ---- 场景绘制 ----
		// 调用前绑定主 FBO：颜色0=HDR、颜色1=实体ID、颜色2=观察空间法线，并提供深度附件。
		static void RenderScene(Scene& scene, const EditorCamera& camera);
		static void RenderRuntime(Scene& scene, const glm::mat4& projection, const glm::mat4& cameraTransform /* 相机到世界空间 */);

		// ---- 渲染参数 ----
		static ShadowSettings& GetShadowSettings();
		static SSAOSettings&   GetSSAOSettings();
		static RSMSettings&    GetRSMSettings();

		// IBL / Shadow / Z-Prepass / SSAO 全局开关（菜单栏 Toggle）
		static bool& GetIBLEnabled();
		static bool& GetShadowEnabled();
		static bool& GetZPrepassEnabled();
		static bool& GetSSAOEnabled();

		// ---- 调试输出 ----
		// 以下 ID 均为借用的 OpenGL 纹理名，目标重建后需重新查询，调用方不得释放。
		// SSAO 纹理（RGBA8 R 通道 = AO 可见度）GL 纹理 ID，供 ImGui 面板预览
		static uint32_t GetSSAOTextureID();
		// 主 FBO 的 view-space 法线附件 GL 纹理 ID（Z-Prepass 写入）
		static uint32_t GetViewNormalTextureID();
		// Shadow Map 深度纹理 ID，供 Shadow 面板预览
		static uint32_t GetShadowMapTextureID();
		// Z-Prepass 的 R8 深度预览纹理，值经过当帧对比拉伸，不是原始深度附件
		static uint32_t GetPrepassDepthTextureID();

		// ---- 后处理 ----
		static BloomSettings&   GetBloomSettings();
		static ToneMapSettings& GetToneMapSettings();
		static uint32_t GetBloomTextureID();
		// 借用本帧主场景 HDR 颜色纹理及像素尺寸，不接管 FBO 或纹理的生命周期。
		static void SetMainSceneColorTarget(uint32_t colorTex, int width, int height);

		// RSM 三张产物纹理（供 Render Passes 面板预览）：
		//   Flux：每单位光源投影面积的反射通量，采样计算时再乘面积。
		//   Normal：表面在世界空间中的朝向，分量从 [-1,1] 映射到 [0,1] 保存。
		//   Distance：从光源方向看到的深度；0 到 1 对应近、远裁剪面，不是世界单位的距离。
		static uint32_t GetRSMFluxTextureID();
		static uint32_t GetRSMNormalTextureID();
		static uint32_t GetRSMDistanceTextureID();
		// 本帧与前帧混合后的间接光图，供面板预览。
		static uint32_t GetRSMResolvedTextureID();
		// 旧结果所占比例：0 只用本帧，0.9 表示旧结果占 90%。
		static float& GetRSMBlendAlpha();

		// 返回后处理的 LDR 输出纹理。
		static uint32_t GetFinalColorTextureID();

	private:
		// 遍历 registry 收集灯光数据并上传 Light UBO
		static void CollectAndUploadLights(Scene& scene);
	};

} // namespace Redflag
