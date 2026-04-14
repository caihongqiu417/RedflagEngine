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

#include "Redflag/Core/AssetManager.h"
#include "Redflag/Renderer/Model.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Redflag {

	// =================================================================
	// 静态资源：Light UBO
	// =================================================================
	static Ref<UniformBuffer> s_LightUBO;

	// CPU 侧灯光缓冲区，每帧填充后一次性上传
	// 布局：[GPULightUBO header (16B)] + [LightData[] 数组]
	static constexpr uint32_t LIGHT_UBO_SIZE =
		sizeof(GPULightUBO) +
		(MAX_DIRECTIONAL_LIGHTS + MAX_POINT_LIGHTS) * sizeof(GPULightData);

	void SceneRenderer::Init()
	{
		s_LightUBO = UniformBuffer::Create(LIGHT_UBO_SIZE, LIGHT_UBO_BINDING);
	}

	void SceneRenderer::Shutdown()
	{
		s_LightUBO.reset();
	}

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

		// ======================== 灯光收集 ========================
		CollectAndUploadLights(scene);

		// ======================== 3D 渲染 ========================
		Renderer3D::BeginScene(camera);

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
					Renderer3D::DrawModel(mr.ModelAsset, transform.GetTransform(), mr.OverrideMaterial, mr.Color, (int)entity);
			});
		}

		Renderer3D::EndScene();
	}

	// ==================================================================
	// RenderRuntime — 运行时模式渲染
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

		// ======================== 灯光收集 ========================
		CollectAndUploadLights(scene);

		// ======================== 3D 渲染 ========================
		glm::mat4 viewProj3D = projection * glm::inverse(cameraTransform);
		glm::vec3 cameraPos  = glm::vec3(cameraTransform[3]); // 从变换矩阵提取位移列
		Renderer3D::BeginScene(viewProj3D, cameraPos);

		{
			auto view = registry.view<TransformComponent, ModelRendererComponent>();
			view.each([&](auto entity, TransformComponent& transform, ModelRendererComponent& mr)
			{
				if (!mr.ModelAsset && !mr.FilePath.empty())
					mr.ModelAsset = AssetManager::GetOrLoad<Model>(mr.FilePath);

				if (mr.ModelAsset)
					Renderer3D::DrawModel(mr.ModelAsset, transform.GetTransform(), mr.OverrideMaterial, mr.Color, (int)entity);
			});
		}

		Renderer3D::EndScene();
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

		RF_CORE_TRACE("Lights: {0} dir, {1} point", header.NumDirectionalLights, header.NumPointLights);

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
