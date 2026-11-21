#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/EditorCamera.h"
#include "Redflag/Renderer/UniformBuffer.h"
#include "Redflag/Renderer/PostProcess.h"

#include <glm/glm.hpp>

namespace Redflag {

	class Scene;

	// 每盏灯占 64 字节，成员顺序与着色器里的 LightData 一致。
	struct GPULightData
	{
		glm::vec4 Direction;  // xyz 沿光线方向，w 为 0 表示方向光，1 表示点光
		glm::vec4 Color;       // xyz = 颜色,       w = 强度
		glm::vec4 Position;    // xyz = 世界坐标,   w = 半径（点光）
		int       CastShadow;  // 0 或 1
		int       _pad[3];  // 着色器每隔 64 字节开始读下一盏灯
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

	// ---- 阴影参数 ----
	struct ShadowSettings
	{
		int   Mode        = 1;       // 0=Off, 1=PCF, 2=PCSS
		float DepthBias   = 0.0015f;  // 减轻表面在自己身上投下的阴影，表面越背向光线，偏移越大
		float NormalBias  = 1.0f;  // 额外的深度偏移，除以阴影图宽度后参与比较
		float PcfRadius   = 1.5f;  // PCF 采样半径，以阴影图像素计
		float LightSize   = 6.0f;  // PCSS 光源大小，以阴影图像素计，值越大阴影边缘越宽
		float OrthoHalfSize = 5.0f;  // 灯光照到的正方形区域半宽（世界单位）。范围越大，每个阴影像素覆盖的面积越大。
	};

	// ---- 屏幕空间环境遮蔽（SSAO）参数 ----
	struct SSAOSettings
	{
		float Radius = 0.5f;    // 观察空间采样半径，与场景长度单位一致
		float Bias   = 0.025f;  // 观察空间深度偏移，与 Radius 同单位
		float Power  = 2.0f;  // 值越大，受遮蔽的地方越暗
		int   KernelSize = 64;  // 每个像素检查多少个邻近位置，最多 64 个
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
	 * 读取场景里的物体和灯光，安排阴影、遮蔽、间接光、主画面和后处理的绘制顺序。
	 * 调用方持有 Scene。这里为一个视口保存绘制目标、上一帧的间接光和渲染开关。
	 * 编辑器提供场景颜色图，PostProcess 保存后处理完成的图像。
	 */
	class SceneRenderer
	{
	public:
		// ---- 初始化与销毁 ----
		// Init/Shutdown 必须在有效图形上下文中调用。
		static void Init();
		static void Shutdown();


		// ---- 场景绘制 ----
		// 调用前选好主画面的绘制目标。它需能保存 HDR 颜色、拾取编号、观察空间法线和深度。
		static void RenderScene(Scene& scene, const EditorCamera& camera);
		static void RenderRuntime(Scene& scene, const glm::mat4& projection, const glm::mat4& cameraTransform /* 相机到世界空间 */);

		// ---- 渲染参数 ----
		static ShadowSettings& GetShadowSettings();
		static SSAOSettings&   GetSSAOSettings();
		static RSMSettings&    GetRSMSettings();

		static bool& GetIBLEnabled();
		static bool& GetShadowEnabled();
		static bool& GetZPrepassEnabled();
		static bool& GetSSAOEnabled();

		// ---- 调试输出 ----
		// 这些预览纹理由渲染器持有，尺寸改变后请重新取 ID。遮蔽图越暗，环境光越弱。
		static uint32_t GetSSAOTextureID();
		// 深度预写时从相机看到的表面法线。
		static uint32_t GetViewNormalTextureID();
		static uint32_t GetShadowMapTextureID();
		// 深度预览把本帧最近到最远的物体映射成黑到白，方便分辨前后。
		static uint32_t GetPrepassDepthTextureID();

		// ---- 后处理 ----
		static BloomSettings&   GetBloomSettings();
		static ToneMapSettings& GetToneMapSettings();
		static uint32_t GetBloomTextureID();
		// 后处理在本帧读取这张场景颜色图，由调用方保管。宽高以像素计。
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
		static void CollectAndUploadLights(Scene& scene);
	};

} // namespace Redflag
