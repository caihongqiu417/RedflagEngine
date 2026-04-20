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
		float OrthoHalfSize = 25.0f; // 光源正交视锥半宽（世界空间）
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

		// IBL / Shadow 全局开关（菜单栏 Toggle）
		static bool& GetIBLEnabled();
		static bool& GetShadowEnabled();

	private:
		// 遍历 registry 收集灯光数据并上传 Light UBO
		static void CollectAndUploadLights(Scene& scene);
	};

} // namespace Redflag
