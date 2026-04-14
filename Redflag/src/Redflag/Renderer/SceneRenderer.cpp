// =====================================================================
// SceneRenderer.cpp — 第一版实现（纯转发）
//
// 这个文件的核心思路：
//   把原来 Scene::OnUpdateEditor 和 Scene::OnUpdateRuntime 中的
//   「遍历实体 → 调用 Renderer2D / Renderer3D」逻辑，原封不动地
//   搬到这里。Scene 不再直接 #include Renderer2D / Renderer3D，
//   渲染调用全部由 SceneRenderer 发出。
//
//   这一步完成后，画面和功能应该与改动前完全一致。
//   后续会在这里拆分出多个 pass（Shadow / Main / PostProcess）。
// =====================================================================

#include "rfpch.h"
#include "SceneRenderer.h"

// 需要访问 Scene 的 registry 和组件
#include "Redflag/Scene/Scene.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Scene/Components.h"

// 底层渲染执行器
#include "Redflag/Renderer/Renderer2D.h"
#include "Redflag/Renderer/Renderer3D.h"

// 资源加载（模型懒加载）
#include "Redflag/Core/AssetManager.h"
#include "Redflag/Renderer/Model.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Redflag {

	// ------------------------------------------------------------------
	// BeginScene / EndScene
	// 第一版为空操作。后续在这里：
	//   BeginScene → 准备帧级 framebuffer、清屏、上传全局 UBO
	//   EndScene   → 提交后处理、解绑 framebuffer
	// ------------------------------------------------------------------
	void SceneRenderer::BeginScene()
	{
	}

	void SceneRenderer::EndScene()
	{
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

		// ======================== 2D 渲染 ========================
		// 使用编辑器相机的 ViewProjection 初始化 2D 批处理
		Renderer2D::BeginScene(camera);

		// 遍历所有带 Transform + Sprite 的实体，提交 2D 四边形
		{
			auto view = registry.view<TransformComponent, SpriteRendererComponent>();
			view.each([&](auto entity, TransformComponent& transform, SpriteRendererComponent& sprite)
			{
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

		// ======================== 3D 渲染 ========================
		// 使用编辑器相机初始化 3D 提交队列
		Renderer3D::BeginScene(camera);

		// --- MeshRendererComponent ---
		// 单个 Mesh，支持纹理或纯色
		{
			auto view = registry.view<TransformComponent, MeshRendererComponent>();
			view.each([&](auto entity, TransformComponent& transform, MeshRendererComponent& mr)
			{
				if (mr.MeshData)
				{
					if (mr.Texture)
						Renderer3D::DrawMesh(mr.MeshData, transform.GetTransform(), mr.Texture, (int)entity);
					else
						Renderer3D::DrawMesh(mr.MeshData, transform.GetTransform(), mr.Color, (int)entity);
				}
			});
		}

		// --- ModelRendererComponent ---
		// 多子网格模型，支持懒加载
		{
			auto view = registry.view<TransformComponent, ModelRendererComponent>();
			view.each([&](auto entity, TransformComponent& transform, ModelRendererComponent& mr)
			{
				// 懒加载：路径有效但 ModelAsset 未加载时，通过 AssetManager 加载
				if (!mr.ModelAsset && !mr.FilePath.empty())
					mr.ModelAsset = AssetManager::GetOrLoad<Model>(mr.FilePath);

				if (mr.ModelAsset)
					Renderer3D::DrawModel(mr.ModelAsset, transform.GetTransform(), mr.Color, (int)entity);
			});
		}

		Renderer3D::EndScene();
	}

	// ==================================================================
	// RenderRuntime — 运行时模式渲染
	//
	// 逻辑等价于原来 Scene::OnUpdateRuntime 中 「if (mainCamera)」
	// 之后的渲染部分。相机数据由调用方传入，而不是 SceneRenderer
	// 自己去查找 CameraComponent。
	//
	// 为什么把「查找主相机」留在 Scene 里：
	//   因为主相机的选择是场景逻辑（哪个 CameraComponent.Primary == true），
	//   不是渲染逻辑。SceneRenderer 只需要最终的投影和 Transform。
	// ==================================================================
	void SceneRenderer::RenderRuntime(Scene& scene, const glm::mat4& projection, const glm::mat4& cameraTransform)
	{
		entt::registry& registry = scene.GetRegistry();

		// ======================== 2D 渲染 ========================
		Renderer2D::BeginScene(projection, cameraTransform);

		{
			auto view = registry.view<TransformComponent, SpriteRendererComponent>();
			view.each([&](auto entity, TransformComponent& transform, SpriteRendererComponent& sprite)
			{
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

		Renderer2D::EndScene();

		// ======================== 3D 渲染 ========================
		// 运行时相机：手动构造 viewProjection = projection * inverse(cameraTransform)
		glm::mat4 viewProj3D = projection * glm::inverse(cameraTransform);
		Renderer3D::BeginScene(viewProj3D);

		{
			auto view = registry.view<TransformComponent, ModelRendererComponent>();
			view.each([&](auto entity, TransformComponent& transform, ModelRendererComponent& mr)
			{
				if (!mr.ModelAsset && !mr.FilePath.empty())
					mr.ModelAsset = AssetManager::GetOrLoad<Model>(mr.FilePath);

				if (mr.ModelAsset)
					Renderer3D::DrawModel(mr.ModelAsset, transform.GetTransform(), mr.Color, (int)entity);
			});
		}

		Renderer3D::EndScene();
	}

} // namespace Redflag
