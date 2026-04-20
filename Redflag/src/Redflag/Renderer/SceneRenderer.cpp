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

	// Shadow 调参：所有字段暴露给 ImGui 面板，运行时可改
	static ShadowSettings s_ShadowSettings;

	// IBL / Shadow 全局开关
	static bool s_IBLEnabled    = true;
	static bool s_ShadowEnabled = true;

	ShadowSettings& SceneRenderer::GetShadowSettings() { return s_ShadowSettings; }
	bool& SceneRenderer::GetIBLEnabled()    { return s_IBLEnabled; }
	bool& SceneRenderer::GetShadowEnabled() { return s_ShadowEnabled; }

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

		s_ShadowDepthShader = Shader::Create("assets/shaders/ShadowDepth.glsl");
	}

	void SceneRenderer::Shutdown()
	{
		s_LightUBO.reset();
		s_ShadowFBO.reset();
		s_ShadowDepthShader.reset();
		IBLPrecompute::Shutdown();
	}

	void SceneRenderer::BeginScene()
	{
	}

	void SceneRenderer::EndScene()
	{
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

		Renderer3D::EndScene();

		// ======================== 天空盒 ========================
		// 放在不透明几何之后、UI/调试线之前
		if (s_IBLEnabled)
			IBLPrecompute::RenderSkybox(camera.GetViewMatrix(), camera.GetProjection());
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
		Renderer3D::BeginScene(viewProj3D, cameraPos);
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

		Renderer3D::EndScene();

		// ======================== 天空盒 ========================
		if (s_IBLEnabled)
		{
			glm::mat4 viewMat3D = glm::inverse(cameraTransform);
			IBLPrecompute::RenderSkybox(viewMat3D, projection);
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
