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

namespace Redflag {

	// 前向声明；SceneRenderer 只需要指针 / 引用访问 Scene
	class Scene;

	class SceneRenderer
	{
	public:
		// ------------------------------------------------------------------
		// BeginScene / EndScene
		// 目前是空操作。后续会在这里初始化帧级资源和 framebuffer。
		// ------------------------------------------------------------------
		static void BeginScene();
		static void EndScene();

		// ------------------------------------------------------------------
		// RenderScene — 编辑器模式下的完整渲染
		//
		// 参数：
		//   scene  — 当前场景（从中读取 ECS registry）
		//   camera — 编辑器相机
		//
		// 做的事：
		//   1. 遍历 2D 实体（Sprite / Circle），用 Renderer2D 画
		//   2. 遍历 3D 实体（Mesh / Model），用 Renderer3D 画
		//   （后续：Shadow → GBuffer → Lighting → Transparent → Overlay → PostProcess）
		// ------------------------------------------------------------------
		static void RenderScene(Scene& scene, const EditorCamera& camera);

		// ------------------------------------------------------------------
		// RenderRuntime — 运行时模式下的完整渲染
		//
		// 参数：
		//   scene           — 当前场景
		//   projection      — 主相机投影矩阵
		//   cameraTransform — 主相机 Transform 矩阵（世界空间）
		//
		// 做的事同 RenderScene，但相机来源不同（是运行时场景里的主相机）
		// ------------------------------------------------------------------
		static void RenderRuntime(Scene& scene, const glm::mat4& projection, const glm::mat4& cameraTransform);
	};

} // namespace Redflag
