#include "rfpch.h"
#include "Redflag/Terrain/TerrainMeshes.h"
#include "LightingPreview.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Platform/Vulkan/VulkanCascadeMaps.h"
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Gameplay/HandContact.h"
#include "Redflag/Renderer/SceneRenderer.h"
#include "Redflag/Scene/Scene.h"
#include "Redflag/Utils/FileSystem.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
#include <glm/gtc/matrix_transform.hpp>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
	static const char* s_Modes[] = { "Base color", "Direct light", "DDGI", "ReSTIR DI", "ReSTIR GI",
		"GTAO", "Clustered lights", "Stochastic reflections", "TAA", "Lit" };
	static const char* s_Labels[] = { "材质底色", "直接光采样", "DDGI", "ReSTIR DI", "ReSTIR GI",
		"GTAO", "分簇光照", "SSR", "TAA", "组合光照 Lit" };
	static const char* s_DirectLabels[] = { "全部光源", "ReSTIR DI", "分簇光照" };
	static const char* s_IndirectLabels[] = { "关闭", "DDGI", "ReSTIR GI", "环境漫反射 IBL" };
	// 场景文件沿用旧名称，面板中的「全部光源」也包括方向光。
	static const char* s_Direct[] = { "All point lights", "ReSTIR DI", "Clustered lights" };
	static const char* s_Indirect[] = { "Off", "DDGI", "ReSTIR GI", "Diffuse IBL" };

	const char* LightingPreview::ModeName(Shading mode) { return s_Labels[int(mode)]; }

	LightingPreview::LightingPreview(VulkanContext& context, VkSampler sampler)
		: m_Context(context), m_Device(context), m_Sampler(sampler)
	{
		m_SurfaceDraw = CreateScope<VulkanDraw>(context, VulkanDraw::Output::LightingSurfaces);
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(context.GetPhysicalDevice(), &properties);
		const VkPhysicalDeviceLimits& limits = properties.limits;
		m_MaxShadowWords = limits.maxStorageBufferRange / sizeof(uint32_t);
		m_MaxExtent = { std::min(limits.maxFramebufferWidth, limits.maxImageDimension2D),
		                std::min(limits.maxFramebufferHeight, limits.maxImageDimension2D) };
		// 每个 ReSTIR 样点最多写四个 vec4，一组计算线程处理 64 个样点。
		m_MaxPixels = std::min<uint64_t>(limits.maxStorageBufferRange / (4 * sizeof(glm::vec4)),
		                                 uint64_t(limits.maxComputeWorkGroupCount[0]) * 64);
		static const uint32_t pack[] =
#include "Platform/Vulkan/Shaders/PackSurfaces.comp.inc"
		;
		m_PackSurfaces = m_Device.CreatePipeline(pack, sizeof(pack),
			{ComputeBinding::Image, ComputeBinding::Image, ComputeBinding::Buffer}, sizeof(glm::mat4));
	}

	LightingPreview::~LightingPreview()
	{
		if (m_Texture)
		{
			m_Context.WaitIdle();
			ImGui_ImplVulkan_RemoveTexture(m_Texture);
		}
	}

	// ---- 场景与光照 ----
	struct SceneGeometry
	{
		std::vector<GIModel> Models;
		std::vector<GIModel> Items;
		std::vector<glm::vec4> Glass;
		std::vector<std::tuple<Ref<SampledTexture>, bool, Ref<SampledTexture>, int, glm::vec2>> Surfaces;
		std::vector<std::pair<uint64_t, float>> LODs;
		std::vector<FoliageState> Plants;
		bool WindMoving = false;
		bool HasAnimatedModel = false;
	};

	static std::optional<std::filesystem::path> ChangedMap(const Ref<Texture2D>& texture, const Ref<Texture2D>& original)
	{
		// 克隆材质时会共享贴图。模型内的图片没有文件名，仍可从原模型读到。
		if (texture == original) return std::nullopt;
		if (!texture) return std::filesystem::path{};
		const std::filesystem::path file(texture->GetPath());
		if (!file.empty() && original && !original->GetPath().empty() &&
			std::filesystem::absolute(file).lexically_normal() == std::filesystem::absolute(original->GetPath()).lexically_normal())
			return std::nullopt;
		return file;
	}

	static SceneGeometry CollectModels(Scene& scene)
	{
		SceneGeometry geometry;
		auto inspect = [&](GIModel& input, size_t part, const Ref<Material>& material, const Material* original, float tintAlpha) {
			geometry.Surfaces.emplace_back(material && material->NormalMap ? material->NormalMap->GetSampledTexture() : nullptr,
			                               material && material->DoubleSided,
			                               material && material->HeightMap ? material->HeightMap->GetSampledTexture() : nullptr,
			                               material ? int(material->HeightMode) : 0,
			                               material ? glm::vec2(material->HeightScale, material->HeightReference) : glm::vec2(0.f));
			input.MaterialFactors.emplace_back(material ? glm::vec2(material->Roughness, material->Metallic) : glm::vec2(.5f, 0.f));
			if (material && material->Alpha == AlphaMode::Mask)
			{
				GIMaskSettings& mask = input.MaterialMasks[part];
				mask.Alpha = material->BaseColor.a * tintAlpha;
				mask.Cutoff = material->AlphaCutoff;
				if (material->AlphaMap && !material->AlphaMap->GetPath().empty())
					mask.Opacity = material->AlphaMap->GetPath();
				else
					mask.Opacity = ChangedMap(material->AlphaMap, original ? original->AlphaMap : nullptr);
			}
			else if (original && original->Alpha == AlphaMode::Mask)
				input.MaterialMasks[part].Cutoff = -1.f;

			if (material && material.get() != original)
			{
				GIMaterialMaps maps;
				maps.BaseColor = ChangedMap(material->BaseColorMap, original ? original->BaseColorMap : nullptr);
				maps.MetallicRoughness = ChangedMap(material->MetallicRoughnessMap, original ? original->MetallicRoughnessMap : nullptr);
				if (maps.BaseColor || maps.MetallicRoughness) input.MaterialMaps.emplace(part, std::move(maps));
			}
			// 把灯牌材质的自发光改成黑色后，要记下这个零值，否则算反射光时灯牌还会亮。
			if (material && (material->Emission != glm::vec3(0) || (original && original->Emission != material->Emission)))
			{
				input.MaterialEmissions.resize(part + 1, glm::vec3(0));
				input.MaterialEmissions[part] = material->Emission;
				const bool imageChanged = original ? material->EmissiveMap != original->EmissiveMap : bool(material->EmissiveMap);
				if (imageChanged || material->DoubleSided != (original && original->DoubleSided))
				{
					auto& settings = input.EmissionOverrides[part];
					settings.DoubleSided = material->DoubleSided;
					if (imageChanged) settings.Image = material->EmissiveMap ? material->EmissiveMap->GetPath() : std::string{};
				}
			}
			if (!material || material->Alpha != AlphaMode::Blend) return;
			input.TransparentParts.resize(part + 1);
			input.TransparentParts[part] = 1;
			geometry.Glass.push_back(material->BaseColor);
			geometry.Glass.emplace_back(material->RefractionIndex, material->Thickness, 0, 0);
		};
		auto& registry = scene.GetRegistry();
		auto collect = [&](entt::entity entity, GIModel input) {
			// 手中的道具每帧跟着骨骼走，地上的物品也能被拿走。两者只需重画表面和动态阴影。
			if (registry.any_of<HandProp, PickupComponent>(entity))
				geometry.Items.push_back(std::move(input));
			else
				geometry.Models.push_back(std::move(input));
		};
		registry.view<TransformComponent, ModelRendererComponent>().each([&](auto entity, auto& transform, auto& model) {
			if (!model.ModelAsset && model.ModelID) model.ModelAsset = AssetManager::Load<Model>(model.ModelID);
			if (!model.ModelAsset) return;
			if (FoliageComponent* patch = registry.try_get<FoliageComponent>(entity))
			{
				Foliage* grass = patch->Visible ? patch->Get() : nullptr;
				if (grass && grass->Count)
				{
					geometry.HasAnimatedModel = true;
					geometry.WindMoving |= patch->Wind && grass->Frequency != 0 && grass->Lean != 0 && grass->Direction != glm::vec2(0);
					geometry.Plants.emplace_back(uint32_t(entity), uint64_t(model.ModelID), patch->Revision,
						patch->Wind, transform.GetTransform(), model.Color);
				}
				// 每株位置已包含在实例绘制中。静态探针不能把源模型再算作地块中央的一株。
				return;
			}
			// 烘焙静态房间时跳过会动的模型。误加播放组件的静态模型仍然参与遮挡。
			if (registry.all_of<ModelAnimationComponent>(entity) && model.ModelAsset->GetAnimation())
			{
				geometry.HasAnimatedModel = true;
				return;
			}
			if (!model.LODs.empty()) geometry.LODs.emplace_back(static_cast<uint64_t>(entity), 0.f);
			for (const auto& lod : model.LODs) geometry.LODs.emplace_back(lod.ModelID, lod.ScreenHeight);
			GIModel input{ model.ModelAsset->GetFilePath(), transform.GetTransform(), glm::vec3(model.Color) };
			input.EntityID = registry.get<IDComponent>(entity).ID;
			for (size_t i = 0; i < model.ModelAsset->GetSubMeshes().size(); ++i)
			{
				const Ref<Material>& original = model.ModelAsset->GetSubMeshes()[i].Mat;
				const Ref<Material>& material = model.ModelAsset->GetMaterial(i, model.OverrideMaterials);
				if (material && (!original || glm::vec3(material->BaseColor) != glm::vec3(original->BaseColor)))
				{
					input.MaterialColors.resize(i + 1, glm::vec4(0));
					input.MaterialColors[i] = glm::vec4(glm::vec3(material->BaseColor), 1);
				}
				inspect(input, i, material, original.get(), model.Color.a);
			}
			collect(entity, std::move(input));
		});
		registry.view<TransformComponent, MeshRendererComponent>().each([&](auto entity, auto& transform, auto& mesh) {
			// 内置的立方体用 MeshRenderer 保存，导入的模型由上面的 ModelRenderer 读取。
			if (!mesh.MeshData) return;
			const Ref<Material>& material = mesh.Mat;
			const glm::vec4 color = material ? material->BaseColor : (mesh.Texture ? glm::vec4(1) : mesh.Color);
			GIModel input{ {}, transform.GetTransform(), glm::vec3(color) };
			input.EntityID = registry.get<IDComponent>(entity).ID;
			inspect(input, 0, material, nullptr, 1.f);
			if (!material && mesh.Texture)
				input.MaterialMaps[0].BaseColor = mesh.Texture->GetPath();
			collect(entity, std::move(input));
		});
		registry.view<TransformComponent, TerrainComponent>().each([&](entt::entity entity, TransformComponent& transform, TerrainComponent& terrain)
		{
			const Ref<TerrainMeshes> surface = TerrainMeshes::Get(terrain);
			if (!surface || surface->GetChunks()->empty()) return;
			GIModel input{ {}, transform.GetTransform(), glm::vec3(terrain.Tint) };
			input.EntityID = registry.get<IDComponent>(entity).ID;
			input.TerrainChunks = surface->GetChunks();
			inspect(input, 0, surface->GetMaterial(), nullptr, 1.f);
			geometry.Models.push_back(std::move(input));
		});
		// 删除一个物品会改变 ECS 的遍历顺序，同一组静态模型仍可沿用上帧的光照数据。
		std::sort(geometry.Models.begin(), geometry.Models.end(), [](const GIModel& a, const GIModel& b) {
			return a.EntityID < b.EntityID;
		});
		return geometry;
	}

	static std::vector<GILight> CollectLights(Scene& scene)
	{
		std::vector<GILight> lights;
		scene.GetRegistry().view<TransformComponent, PointLightComponent>().each([&](auto& transform, auto& light) {
			lights.push_back({ glm::vec4(transform.Translation, 1), glm::vec4(light.Color * light.Intensity, light.Radius) });
		});
		scene.GetRegistry().view<TransformComponent, DirectionalLightComponent>().each([&](auto& transform, auto& light) {
			const glm::vec3 direction = light.WorldDirection(transform);
			if (glm::dot(direction, direction) >= 1e-6f)
				lights.push_back(GILight::Directional(direction, light.Color * light.Intensity, light.CastShadow));
		});
		return lights;
	}

	// ---- 动画角色的方向光阴影 ----
	void LightingPreview::DrawAnimatedShadows(Scene& scene, const std::vector<GILight>& lights)
	{
		const size_t count = std::count_if(lights.begin(), lights.end(), [](const GILight& light) {
			return light.Position.w == -1.f;
		});
		glm::vec3 minimum, maximum;
		if (!m_ShadowSize || !count || !SceneRenderer::GetAnimatedBounds(scene, minimum, maximum))
		{
			m_GI->SetAnimatedVisibility(nullptr);
			m_ShadowMaps.reset();
			m_ShadowWords = 0;
			m_ShadowDraw.reset();
			return;
		}

		// 开头每盏灯占一个索引。每张图前面有 16 个矩阵元素、图宽和每格的世界尺寸。
		uint32_t side = uint32_t(m_ShadowSize);
		auto words = [&] { return lights.size() + count * (18 + size_t(side) * side); };
		while (side > 1 && words() > m_MaxShadowWords) side /= 2;
		if (words() > m_MaxShadowWords) throw std::runtime_error("Directional shadow maps exceed the GPU buffer size");
		if (words() != m_ShadowWords) {
			m_ShadowWords = words();
			m_ShadowMaps = m_Device.CreateBuffer(m_ShadowWords * sizeof(uint32_t));
		}

		struct ShadowView { glm::mat4 VP; uint32_t Lamp, First; float Texel; };
		if (!m_StoreShadow) {
			static constexpr uint32_t code[] =
#include "Shaders/StoreShadow.comp.inc"
			;
			m_StoreShadow = m_Device.CreatePipeline(code, sizeof(code), {ComputeBinding::Image, ComputeBinding::Buffer}, sizeof(ShadowView));
		}
		if (!m_ShadowDraw) m_ShadowDraw = CreateScope<VulkanDraw>(m_Context, VulkanDraw::Output::LightingSurfaces);
		m_ShadowDraw->Resize(side, side);

		// 角色离开相机以后，影子仍可能落在画面里。从光源一侧包住本帧所有角色。
		const glm::vec3 center = (minimum + maximum) * .5f;
		const float radius = std::max(glm::length(maximum - minimum) * .5f, .001f);
		const float half = radius * (1.f + 2.f / side);
		const glm::mat4 projection = glm::ortho(-half, half, -half, half, 0.f, 2.f * half);
		uint32_t first = uint32_t(lights.size());
		for (uint32_t i = 0; i < lights.size(); ++i)
		{
			if (lights[i].Position.w != -1.f) continue;
			const glm::vec3 toLight(lights[i].Position);
			const glm::vec3 up = std::abs(toLight.y) < .99f ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
			const ShadowView light{projection * glm::lookAt(center + half * toLight, center, up), i, first, 2.f * half / side};
			m_ShadowDraw->DrawSurfaces([&](CommandList& commands) {
				SceneRenderer::DrawGeometry(scene, commands, light.VP, nullptr, true);
			});
			m_Device.Dispatch(m_StoreShadow, {m_ShadowDraw->GetNormalDepth(), m_ShadowMaps}, &light, (side+7)/8, (side+7)/8);
			first += 18 + side * side;
		}
		m_GI->SetAnimatedVisibility(m_ShadowMaps);
	}

	void LightingPreview::DrawCascadeShadows(Scene& scene, const std::vector<GILight>& lights,
		const glm::mat4& projection, const glm::mat4& view)
	{
		glm::vec3 low, high;
		const glm::mat4 cameraVP = projection * view;
		const bool hasSun = std::any_of(lights.begin(), lights.end(), [](const GILight& lamp) { return lamp.Position.w == -1.f; });
		if (!hasSun || !SceneRenderer::GetShadowBounds(scene, low, high, false, cameraVP))
		{
			m_GI->SetAnimatedVisibility(nullptr);
			m_CascadeMaps.reset();
			return;
		}

		if (!m_CascadeMaps)
		{
			m_ShadowDraw.reset();
			m_ShadowMaps.reset();
			m_ShadowWords = 0;
			m_CascadeMaps = CreateScope<VulkanCascadeMaps>(m_Context, m_Device);
		}
		Ref<ComputeResource> maps = m_CascadeMaps->Draw(projection, view, lights, low, high, m_CascadeCount, m_CascadeSize,
			m_ShadowDistance, m_SunRadius, [&](CommandList& commands, const glm::mat4& lightVP) {
				SceneRenderer::DrawGeometry(scene, commands, lightVP, &cameraVP);
			});
		m_GI->SetAnimatedVisibility(maps);
	}

	static const ModelRendererComponent* MissingBakeModel(Scene& scene)
	{
		auto models = scene.GetRegistry().view<ModelRendererComponent>();
		for (entt::entity entity : models)
		{
			const ModelRendererComponent& model = models.get<ModelRendererComponent>(entity);
			if (!model.ModelAsset && (model.ModelID || !model.FilePath.empty()))
				return &model;
		}
		return nullptr;
	}

	static void CheckModelsForBake(Scene& scene)
	{
		if (const ModelRendererComponent* missing = MissingBakeModel(scene))
			throw std::runtime_error("Cannot bake lighting without model: " + missing->FilePath);
	}

	bool LightingPreview::RefreshEnvironment(const std::filesystem::path& path, bool readAgain)
	{
		std::error_code error;
		const auto time = path.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(path, error);
		if (!readAgain && path == m_EnvironmentPath && time == m_EnvironmentTime) return false;

		uint64_t key = path.empty() ? 0 : m_EnvironmentKey;
		if (path.empty()) m_Panorama = {};
		Ref<ComputeResource> lighting = path.empty() ? nullptr : EnvironmentLighting::Load(m_Device, path, &key, m_Environment, &m_Panorama);
		const bool changed = path != m_EnvironmentPath || key != m_EnvironmentKey || bool(lighting) != bool(m_Environment);
		if (!lighting && !path.empty()) RF_CORE_WARN("Cannot load lighting environment: {0}", path.u8string());
		m_EnvironmentPath = path;
		m_EnvironmentTime = time;
		if (changed) {
			m_Environment = std::move(lighting);
			m_EnvironmentKey = key;
			ResetHistory();
		}
		return changed;
	}

	Scope<GlobalIllumination> LightingPreview::CreateProbeLighting(const std::vector<GIModel>& models,
		const std::vector<GILight>& lights, glm::uvec2 size)
	{
		if (!m_EnvironmentPath.empty() && !m_Environment)
			throw std::runtime_error("烘焙需要的环境图无法读取：" + m_EnvironmentPath.u8string());
		// 只读写烘焙文件时用 1 × 1 的视口。编辑器还要用这份光照绘制场景，传入实际画幅。
		Scope<GlobalIllumination> lighting = CreateScope<GlobalIllumination>(m_Device, models, size.x, size.y);
		lighting->SetLights(lights);
		lighting->SetProbeFeedback(m_ProbeFeedback);
		lighting->SetProbeEnvironment(m_Environment, m_EnvironmentKey);
		return lighting;
	}

	void LightingPreview::BakeProbes(Scene& scene)
	{
		const SceneGeometry geometry = CollectModels(scene);
		CheckModelsForBake(scene);
		RefreshEnvironment(scene.GetEnvironment(), true);
		CreateProbeLighting(geometry.Models, CollectLights(scene))->BakeProbes(m_ProbeFile);
	}

	VkDescriptorSet LightingPreview::Render(Scene& scene, const glm::mat4& projection, const glm::mat4& view,
		glm::vec3 eye, uint32_t viewportWidth, uint32_t viewportHeight, bool navigating, bool scenePreview)
	{
		SceneGeometry geometry = CollectModels(scene);
		if (m_Decals.Update(scene)) ResetHistory();
		// 角色的骨段会改变相机看到的表面。房间的静态三角形和已烘焙的探针继续使用。
		std::vector<glm::mat4> bones;
		scene.GetRegistry().view<TransformComponent, SkeletonAnimationComponent>().each(
			[&](const TransformComponent& transform, const SkeletonAnimationComponent& player) {
				const glm::mat4 world = transform.GetTransform();
				for (const glm::mat4& bone : player.Bones) bones.push_back(world * bone);
			});
		for (entt::entity entity : scene.GetRegistry().view<RagdollComponent>())
			for (const glm::mat4& limb : scene.GetRagdollMeshes(entity)) bones.push_back(limb);
		std::vector<Ref<Mesh>> meshes;
		scene.GetRegistry().view<TransformComponent, ModelAnimationComponent>().each(
			[&](const TransformComponent& transform, const ModelAnimationComponent& player) {
				const auto& posed = player.GetMeshes();
				for (size_t i = 0; i < posed.size(); ++i) {
					meshes.push_back(posed[i]);
					bones.push_back(transform.GetTransform() * player.GetTransform(i));
				}
			});
		if (meshes != m_AnimatedMeshes || bones != m_Bones) {
			m_AnimatedMeshes = std::move(meshes);
			m_Bones = std::move(bones);
			ResetHistory();
		}
		// 改了简化模型的设置，场景面板就要重画。算反射光时仍用原来的精细模型。
		if (geometry.Plants != m_Foliage) { m_Foliage = std::move(geometry.Plants); ResetHistory(); }
		if (geometry.LODs != m_LODSettings) { m_LODSettings = std::move(geometry.LODs); ResetHistory(); }
		if (geometry.Glass != m_GlassSettings) { m_GlassSettings = std::move(geometry.Glass); ResetHistory(); }
		// 换了法线贴图，墙面的明暗就要重画。探针使用三角形的法线，原来的烘焙仍可用。
		if (geometry.Surfaces != m_SurfaceSettings) { m_SurfaceSettings = std::move(geometry.Surfaces); ResetHistory(); }
		const bool environmentChanged = RefreshEnvironment(scene.GetEnvironment(), m_BakeRequested);
		if (m_SkyVisible != scene.IsSkyVisible())
		{
			m_SkyVisible = scene.IsSkyVisible();
			ResetHistory();
		}
		const std::vector<GILight> lights = CollectLights(scene);
		const bool hasActor = geometry.HasAnimatedModel || !m_Bones.empty() || !geometry.Items.empty();
		// 只放角色、尚未布光时，沿用材质底色来预览动作。加灯或选择环境后按正常光照绘制。
		m_OverlayModels = geometry.Models.empty() && hasActor && lights.empty() && !m_Environment;
		const bool emptyScene = geometry.Models.empty() && (!hasActor || m_OverlayModels);
		if (emptyScene)
		{
			// 场景里已经没有可绘制的模型。清掉场景的光照探针，取消尚未开始的烘焙。
			if (m_GI)
			{
				m_GI.reset();
				m_Models.clear();
				m_ProbeMessage.clear();
				m_BakeRequested = false;
				ResetHistory();
			}
			// 没有需要照亮的表面时，面板只需一张天空图。
			if (Mode != Shading::Lit || !m_SkyVisible || !m_Environment)
				return VK_NULL_HANDLE;
		}
		const uint32_t panelWidth = scenePreview && m_PreviewWidth > 0 ? std::min(viewportWidth, uint32_t(m_PreviewWidth)) : viewportWidth;
		const uint32_t panelHeight = std::max(1u, uint32_t(std::lround(double(viewportHeight) * panelWidth / viewportWidth)));
		const bool fits = panelWidth <= m_MaxExtent.width / 2 && panelHeight <= m_MaxExtent.height / 2 &&
		                  uint64_t(panelWidth) * panelHeight <= m_MaxPixels / 4;
		const uint32_t sampleSide = !emptyScene && m_Supersampling && fits ? 2u : 1u;
		const uint32_t width = panelWidth * sampleSide, height = panelHeight * sampleSide;
		bool resized = width != m_Width || height != m_Height || sampleSide != m_SampleSide;
		if (resized)
		{
			m_Context.WaitIdle();
			if (m_Texture) ImGui_ImplVulkan_RemoveTexture(m_Texture);
			m_EmittedColor.reset();
			m_Image = m_Device.CreateImage(panelWidth, panelHeight);
			m_Texture = ImGui_ImplVulkan_AddTexture(m_Sampler, m_Device.GetImageView(m_Image), VK_IMAGE_LAYOUT_GENERAL);
			m_Effects.reset();
			m_CombinedLight.reset();
			m_TransparentColor.reset();
			m_Width = width;
			m_Height = height;
			m_SampleSide = sampleSide;
			RF_CORE_INFO("Lighting preview {0} x {1}, viewport {2} x {3}", width, height, viewportWidth, viewportHeight);
		}
		if (emptyScene)
		{
			if (!resized && m_StillFrames && m_LastViewProjection == projection * view) return m_Texture;
			if (!m_CombinedLight) m_CombinedLight = m_Device.CreateBuffer(size_t(width) * height * sizeof(glm::vec4));
			DrawSky(m_CombinedLight, projection, view);
			m_LastViewProjection = projection * view;
			return Display(m_CombinedLight);
		}

		auto sameModel = [](const GIModel& a, const GIModel& b) {
			return a.EntityID == b.EntityID && a.Path == b.Path && a.Transform == b.Transform && a.Tint == b.Tint && a.TerrainChunks == b.TerrainChunks && a.MaterialColors == b.MaterialColors &&
				a.MaterialFactors == b.MaterialFactors && a.MaterialEmissions == b.MaterialEmissions &&
				a.EmissionOverrides == b.EmissionOverrides && a.TransparentParts == b.TransparentParts &&
				a.MaterialMaps == b.MaterialMaps && a.MaterialMasks == b.MaterialMasks;
		};
		if (!std::equal(geometry.Items.begin(), geometry.Items.end(), m_Items.begin(), m_Items.end(), sameModel))
		{
			m_Items = std::move(geometry.Items);
			ResetHistory();
		}
		bool geometryChanged = !m_GI || !std::equal(geometry.Models.begin(), geometry.Models.end(), m_Models.begin(), m_Models.end(), sameModel);
		if (geometryChanged)
		{
			// 换了模型、移动了物体或改了材质，就重新准备光线可能碰到的三角形和表面颜色。
			m_GI = CreateScope<GlobalIllumination>(m_Device, geometry.Models, width, height);
			m_Models = std::move(geometry.Models);
			ResetHistory();
		}
		else if (resized)
		{
			m_GI->Resize(width, height);
			ResetHistory();
		}
		if (!m_Effects)
		{
			m_SurfaceDraw->Resize(width, height);
			m_SurfaceDraw->SetSubpixelMask(sampleSide == 2);
			m_Effects = CreateScope<ScreenEffects>(m_Device, width, height);
		}

		const auto& previousLights = m_GI->GetLights();
		const bool sameLights = std::equal(lights.begin(), lights.end(), previousLights.begin(), previousLights.end(),
			[](const GILight& a, const GILight& b) { return a.Position == b.Position && a.Intensity == b.Intensity; });
		if (!sameLights || Mode != m_PreviousMode)
			ResetHistory();
		m_GI->SetLights(lights);
		const bool probeSkyChanged = m_GI->SetProbeEnvironment(m_Environment, m_EnvironmentKey);
		const bool feedbackChanged = m_GI->SetProbeFeedback(m_ProbeFeedback);
		if (!m_ProbeFile.empty() && (geometryChanged || !sameLights || feedbackChanged || probeSkyChanged || environmentChanged))
		{
			m_ProbeMessage.clear();
			if (!m_Environment && !m_EnvironmentPath.empty())
				m_ProbeMessage = "场景指定的环境图无法读取，请检查文件。";
			else if (std::filesystem::is_regular_file(m_ProbeFile) && !m_GI->LoadProbes(m_ProbeFile))
				m_ProbeMessage = "烘焙文件与当前场景不符或已损坏，请重新烘焙。";
		}
		if (m_BakeRequested)
		{
			m_BakeRequested = false;
			try
			{
				CheckModelsForBake(scene);
				// 按下按钮后重新读磁盘上的模型和贴图。文件写好后，场景面板才换用新的光照。
				Scope<GlobalIllumination> lighting = CreateProbeLighting(m_Models, lights, { width, height });
				lighting->BakeProbes(m_ProbeFile);
				m_GI = std::move(lighting);
				m_ProbeMessage.clear();
				ResetHistory();
			}
			catch (const std::exception& e)
			{
				m_ProbeMessage = e.what();
				RF_CORE_ERROR("Probe bake failed: {0}", m_ProbeMessage);
			}
		}
		const bool cameraMoved = projection * view != m_LastViewProjection;
		if (cameraMoved || geometry.WindMoving) m_StillFrames = 0;

		// 镜头固定时，普通直接光只需算一帧。TAA 等效果会用到前几帧的结果，面板最多算 32 帧，然后沿用最后一张图。
		// 32 是编辑器选用的帧数。调大它，TAA 能多取几次样，显卡也要多画几帧。
		constexpr uint32_t maxPreviewFrames = 32;
		const bool directOnly = CompareDirect || navigating;
		const uint32_t framesToRender = UsesFrameHistory(directOnly) ? maxPreviewFrames : 1;
		if (m_StillFrames >= framesToRender)
		{
			RF_PROFILE_SCOPE("Lighting cached");
			return m_Texture;
		}
		RF_PROFILE_SCOPE(navigating ? "Lighting navigation" : (cameraMoved ? "Lighting camera motion" : "Lighting accumulation"));
		m_PreviousMode = Mode;
		m_LastViewProjection = projection * view;

		// TAA 每帧在像素内换个位置取样。上面先检查相机矩阵，否则取样位置一变，也会被当成镜头移动。
		const bool useTAA = Mode == Shading::TAA || (Mode == Shading::Lit && m_TAA && !directOnly);
		glm::mat4 sampleProjection = useTAA
			? ScreenEffects::JitterProjection(projection, m_Frame, width, height) : projection;
		const glm::mat4 sampleVP = sampleProjection * view;
		const bool redraw = geometryChanged || geometry.WindMoving || resized || m_Frame == 0 || sampleVP != m_GI->GetViewProjection();
		m_GI->SetRasterCamera(sampleVP, eye, cameraMoved);
		if (redraw)
		{
			RF_PROFILE_SCOPE("Rasterize lighting surfaces");
			auto draw = [&](CommandList& commands) { SceneRenderer::DrawGeometry(scene, commands, sampleVP, &m_LastViewProjection); };
			m_SurfaceDraw->DrawSurfaces(draw);
			const bool showEmission = Mode == Shading::Lit || Mode == Shading::Direct || Mode == Shading::Reflections || Mode == Shading::TAA;
			if (showEmission && m_SurfaceDraw->HasEmission())
			{
				if (!m_EmissionDraw) m_EmissionDraw = CreateScope<VulkanDraw>(m_Context, VulkanDraw::Output::Emission);
				m_EmissionDraw->UseSceneDepth(*m_SurfaceDraw);
				m_EmissionDraw->DrawSurfaces(draw);
			}
			else
			{
				m_EmissionDraw.reset();
				m_EmittedColor.reset();
			}
			if (m_SurfaceDraw->HasTransparency())
			{
				if (!m_TransparentDraw) m_TransparentDraw = CreateScope<VulkanDraw>(m_Context, VulkanDraw::Output::Transparency);
				m_TransparentDraw->UseSceneDepth(*m_SurfaceDraw);
				m_TransparentDraw->DrawSurfaces(draw);
			}
			const glm::mat4 inverseVP = glm::inverse(sampleVP);
			m_Device.Dispatch(m_PackSurfaces, {m_SurfaceDraw->GetAlbedo(), m_SurfaceDraw->GetNormalDepth(),
				m_GI->GetSurfaces()}, &inverseVP, (width + 7) / 8, (height + 7) / 8);
			m_Decals.Draw(m_GI->GetSurfaces(), sampleVP, {width, height});
			// 分层范围跟随相机。TAA 只改变像素里的取样点，仍可沿用上次的阴影图。
			if (m_CascadeCount)
			{
				if (cameraMoved || m_Frame == 0 || geometry.WindMoving)
					DrawCascadeShadows(scene, lights, projection, view);
			}
			else if (m_Frame == 0 || geometry.WindMoving)
			{
				m_CascadeMaps.reset();
				DrawAnimatedShadows(scene, lights);
			}
		}
		ScreenEffectSettings& settings = m_Effects->GetSettings();
		settings = m_Settings;
		settings.DiffuseEnvironment = Mode == Shading::Lit && !directOnly && m_IndirectLight == EnvironmentDiffuse;
		settings.ReflectProbeLight = Mode == Shading::Lit && !directOnly && m_IndirectLight == ProbeLighting;
		m_Effects->SetEnvironment(m_Environment);
		Ref<ComputeResource> pixels;
		if (Mode == Shading::Lit)
		{
			pixels = RenderLit(sampleProjection, view, directOnly);
			DrawSky(pixels, sampleProjection, view);
			if (!directOnly && m_FogEnabled) pixels = m_GI->RenderFog(pixels, m_Fog);
			pixels = CompositeTransparency(pixels, sampleVP);
			if (!directOnly && m_TAA)
				pixels = m_Effects->Render(ScreenEffect::TAA, *m_GI, sampleProjection, view, m_Frame, pixels, &projection);
		}
		else
		{
			GIMethod method = GIMethod::Direct;
			if (Mode == Shading::GTAO || Mode == Shading::Reflections || Mode == Shading::TAA)
				method = GIMethod::DirectReference;
			if (Mode == Shading::DDGI) method = GIMethod::DDGI;
			if (Mode == Shading::ReSTIRDI) method = GIMethod::ReSTIRDI;
			if (Mode == Shading::ReSTIRGI) method = GIMethod::ReSTIRGI;
			m_GI->Render(method, m_Samples);
	
			pixels = m_GI->GetOutput();
			if (Mode == Shading::Direct || Mode == Shading::Reflections || Mode == Shading::TAA)
				pixels = AddEmission(pixels);
			if (Mode >= Shading::GTAO)
			{
				constexpr ScreenEffect effects[] = { ScreenEffect::GTAO, ScreenEffect::ClusteredLighting,
					ScreenEffect::StochasticReflections, ScreenEffect::TAA };
				pixels = m_Effects->Render(effects[int(Mode) - int(Shading::GTAO)], *m_GI, sampleProjection,
					view, m_Frame, pixels, &projection);
			}
		}
		VkDescriptorSet texture = Display(pixels);
		// 运动帧混有重投影的旧颜色。等镜头真正停下，再取完整的 32 个样本。
		if (useTAA && cameraMoved && m_Frame > 1) m_StillFrames = 0;
		return texture;
	}

	std::vector<glm::u8vec4> LightingPreview::ReadDisplayPixels()
	{
		const size_t count = size_t(m_Width / m_SampleSide) * (m_Height / m_SampleSide);
		std::vector<glm::vec4> display(count);
		m_Device.Readback(m_Image, display.data(), count * sizeof(glm::vec4));

		std::vector<glm::u8vec4> pixels(count);
		for (size_t i = 0; i < count; ++i)
			pixels[i] = glm::u8vec4(glm::round(glm::clamp(display[i], 0.f, 1.f) * 255.f));
		return pixels;
	}

	VkDescriptorSet LightingPreview::Display(const Ref<ComputeResource>& color)
	{
		struct DisplaySettings { uint32_t Gray, SampleSide, Filmic; float Exposure; };
		if (!m_Display)
		{
			static const uint32_t code[] =
#include "Shaders/LightingPreview.comp.inc"
			;
			m_Display = m_Device.CreatePipeline(code, sizeof(code), { ComputeBinding::Buffer, ComputeBinding::Image }, sizeof(DisplaySettings));
		}
		const bool lit = Mode == Shading::Lit;
		const DisplaySettings display{ uint32_t(Mode == Shading::GTAO), m_SampleSide,
			uint32_t(lit && m_Filmic), lit ? std::exp2(m_ExposureEV) : 1.f };
		m_Device.Dispatch(m_Display, { color, m_Image }, &display, (m_Width / m_SampleSide + 7) / 8, (m_Height / m_SampleSide + 7) / 8);
		++m_Frame;
		++m_StillFrames;
		return m_Texture;
	}

	Ref<ComputeResource> LightingPreview::RenderLit(const glm::mat4& projection, const glm::mat4& view,
		bool directOnly)
	{
		Ref<ComputeResource> direct;
		if (m_DirectLight == ClusteredLights)
			direct = m_Effects->Render(ScreenEffect::ClusteredLighting, *m_GI, projection, view, m_Frame);
		else
		{
			m_GI->RenderDirect(m_DirectLight == ReSTIRDirect);
			direct = m_GI->GetDirectOutput();
		}
		if (directOnly) return AddEmission(direct);

		if (!m_Combine)
		{
			static const uint32_t code[] =
#include "Shaders/CombineLight.comp.inc"
			;
			m_Combine = m_Device.CreatePipeline(code, sizeof(code), std::vector<ComputeBinding>(4, ComputeBinding::Buffer), 3 * sizeof(uint32_t));
		}
		if (!m_CombinedLight) m_CombinedLight = m_Device.CreateBuffer(size_t(m_Width) * m_Height * sizeof(glm::vec4));

		// 点光源照到表面的亮度已经算好。下面按选择的方式，再加上周围物体或环境贴图照过来的光。
		Ref<ComputeResource> indirect = direct;
		const bool indirectEnabled = m_IndirectLight != NoIndirectLight && (m_IndirectLight != EnvironmentDiffuse || m_Environment);
		if (indirectEnabled)
		{
			if (m_IndirectLight == EnvironmentDiffuse)
				indirect = m_Effects->Render(ScreenEffect::EnvironmentDiffuse, *m_GI, projection, view, m_Frame);
			else
			{
				m_GI->Render(m_IndirectLight == ProbeLighting ? GIMethod::DDGI : GIMethod::ReSTIRGI, m_Samples);
				indirect = m_GI->GetOutput();
			}
		}
		const bool useAO = m_AO && indirectEnabled;
		Ref<ComputeResource> ao = useAO ? m_Effects->Render(ScreenEffect::GTAO, *m_GI, projection, view, m_Frame, direct) : direct;
		uint32_t combine[] = { m_Width * m_Height, uint32_t(indirectEnabled), uint32_t(useAO) };
		m_Device.Dispatch(m_Combine, { direct, indirect, ao, m_CombinedLight }, combine, (combine[0] + 63) / 64);
		Ref<ComputeResource> sceneColor = AddEmission(m_CombinedLight);
		if (m_Reflections || (m_Settings.SpecularEnvironment && m_Environment))
		{
			m_Effects->GetSettings().TraceReflections = m_Reflections;
			sceneColor = m_Effects->Render(ScreenEffect::MaterialReflections, *m_GI, projection, view, m_Frame, sceneColor);
		}
		return sceneColor;
	}

	void LightingPreview::DrawSky(const Ref<ComputeResource>& color, const glm::mat4& projection, const glm::mat4& view)
	{
		if (!m_SkyVisible || !m_Environment) return;
		struct SkyView { glm::mat4 InverseSkyVP; glm::ivec2 PanoramaSize; uint32_t HasSurfaces; };
		if (!m_Sky)
		{
			static const uint32_t code[] =
#include "Redflag/Renderer/ScreenSpace/Shaders/SkyBackground.comp.inc"
			;
			m_Sky = m_Device.CreatePipeline(code, sizeof(code),
				{ComputeBinding::Buffer, ComputeBinding::Image, ComputeBinding::Buffer}, sizeof(SkyView));
		}
		// 天空只随镜头转动。没有物体时整张图都画天空，此时着色器只用面板图的宽高。
		SkyView camera{ glm::inverse(projection * glm::mat4(glm::mat3(view))), m_Panorama.Size, uint32_t(bool(m_GI)) };
		const Ref<ComputeResource>& surfaces = m_GI ? m_SurfaceDraw->GetAlbedo() : m_Image;
		const Ref<ComputeResource>& sky = m_Panorama.Pixels ? m_Panorama.Pixels : m_Environment;
		m_Device.Dispatch(m_Sky, {sky, surfaces, color}, &camera, (m_Width + 7) / 8, (m_Height + 7) / 8);
	}

	Ref<ComputeResource> LightingPreview::AddEmission(const Ref<ComputeResource>& reflected)
	{
		if (!m_EmissionDraw) return reflected;
		if (!m_AddEmission)
		{
			static const uint32_t code[] =
#include "Shaders/AddEmission.comp.inc"
			;
			m_AddEmission = m_Device.CreatePipeline(code, sizeof(code),
				{ComputeBinding::Buffer, ComputeBinding::Image, ComputeBinding::Buffer, ComputeBinding::Image}, 0);
		}
		if (!m_EmittedColor) m_EmittedColor = m_Device.CreateBuffer(size_t(m_Width) * m_Height * sizeof(glm::vec4));
		m_Device.Dispatch(m_AddEmission, {reflected, m_EmissionDraw->GetEmission(), m_EmittedColor, m_SurfaceDraw->GetAlbedo()}, nullptr,
			(m_Width + 7) / 8, (m_Height + 7) / 8);
		return m_EmittedColor;
	}

	Ref<ComputeResource> LightingPreview::CompositeTransparency(const Ref<ComputeResource>& background, const glm::mat4& vp)
	{
		if (!m_SurfaceDraw->HasTransparency()) return background;
		if (!m_CompositeTransparency)
		{
			static const uint32_t code[] =
#include "Platform/Vulkan/Shaders/CompositeTransparency.comp.inc"
			;
			m_CompositeTransparency = m_Device.CreatePipeline(code, sizeof(code),
				{ComputeBinding::Buffer, ComputeBinding::Image, ComputeBinding::Image, ComputeBinding::Image,
				 ComputeBinding::Image, ComputeBinding::Buffer}, 2 * sizeof(glm::mat4));
		}
		if (!m_TransparentColor)
			m_TransparentColor = m_Device.CreateBuffer(size_t(m_Width) * m_Height * sizeof(glm::vec4));
		glm::mat4 camera[] = {vp, glm::inverse(vp)};
		m_Device.Dispatch(m_CompositeTransparency, {background, m_TransparentDraw->GetAlbedo(),
			m_TransparentDraw->GetNormalDepth(), m_TransparentDraw->GetOptics(), m_SurfaceDraw->GetNormalDepth(),
			m_TransparentColor}, camera, (m_Width + 7)/8, (m_Height + 7)/8);
		return m_TransparentColor;
	}

	void LightingPreview::ResetHistory()
	{
		m_Frame = 0;
		m_StillFrames = 0;
		if (m_GI) m_GI->ResetHistory();
		if (m_Effects) m_Effects->ResetHistory();
	}

	// ---- 编辑器设置 ----
	bool LightingPreview::UsesFrameHistory(bool directOnly) const
	{
		// 房间的反射光已经烘焙好时，镜头不动，读同一组探针会得到相同的颜色。
		const bool updateProbes = !m_GI->HasBakedProbes();
		switch (Mode)
		{
		case Shading::TAA:
		case Shading::ReSTIRDI:
		case Shading::ReSTIRGI:
			return true;
		case Shading::DDGI:
			return updateProbes;
		case Shading::Lit:
			// 只看直接光时，ReSTIR DI 仍会用上一帧选出的灯。
			if (m_DirectLight == ReSTIRDirect) return true;
			if (directOnly) return false;
			return m_TAA || m_IndirectLight == ReSTIRIndirect || (m_IndirectLight == ProbeLighting && updateProbes);
		default:
			return false;
		}
	}

	bool LightingPreview::DrawLitOptions()
	{
		bool changed = false;
		ImGui::TextUnformatted("直接光");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("光从点光源或方向光直接照到物体表面。");
		ImGui::SetNextItemWidth(-1);
		changed |= ImGui::Combo("##Direct", &m_DirectLight, s_DirectLabels, IM_ARRAYSIZE(s_DirectLabels));
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("“全部光源”逐盏计算场景中的点光源和方向光。");
		const std::string shadowWidth = m_ShadowSize ? std::to_string(m_ShadowSize) : "关闭";
		ImGui::BeginDisabled(m_CascadeCount > 0);
		if (ImGui::BeginCombo("角色阴影", shadowWidth.c_str())) {
			for (int side : {0, 256, 512, 1024}) {
				const std::string name = side ? std::to_string(side) : "关闭";
				if (ImGui::Selectable(name.c_str(), m_ShadowSize == side)) { m_ShadowSize = side; changed = true; }
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip(m_CascadeCount ? "级联阴影已包含角色。每层分辨率决定角色投影的精细程度。"
				: "每盏开启投影的方向光都绘制角色深度。图越宽，阴影轮廓越清楚，占用显存越多。");
		if (ImGui::TreeNode("级联阴影 CSM"))
		{
			changed |= ImGui::SliderInt("级联层数", &m_CascadeCount, 0, 4);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 沿用原阴影。开启后按离相机的距离分层绘制，近处的阴影纹素更密。");
			if (m_CascadeCount)
			{
				changed |= ImGui::SliderInt("每层分辨率", &m_CascadeSize, 128, 2048);
				changed |= ImGui::SliderFloat("阴影距离", &m_ShadowDistance, 1.f, 250.f, "%.1f m");
				changed |= ImGui::SliderFloat("太阳角半径", &m_SunRadius, 0.f, 5.f, "%.2f°");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("设为 0 时只有阴影图的边缘过滤。增大角半径，离地越高的物体会投下越宽的半影。");
			}
			ImGui::TreePop();
		}
		ImGui::TextUnformatted("间接光");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("光先照到其他表面，再反射到这里。");
		ImGui::SetNextItemWidth(-1);
		changed |= ImGui::Combo("##Indirect", &m_IndirectLight, s_IndirectLabels, IM_ARRAYSIZE(s_IndirectLabels));
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("DDGI 或 ReSTIR GI 计算场景里的漫反射；选择环境漫反射 IBL 时，按表面朝向从所选环境取光。");
		if (m_IndirectLight == EnvironmentDiffuse && !m_Environment)
			ImGui::TextWrapped("请在“环境贴图”菜单选择 HDR 全景或烘焙文件。");
		ImGui::BeginDisabled(m_IndirectLight == NoIndirectLight);
		changed |= ImGui::Checkbox("GTAO", &m_AO);
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip(m_IndirectLight != NoIndirectLight ? "减弱墙角等遮挡处的间接光。" : "先选择一种间接光，再用 GTAO 减弱遮挡处的反光。");
		changed |= ImGui::Checkbox("SSR", &m_Reflections);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("用当前画面中能看到的物体计算表面的倒影。");
		ImGui::BeginDisabled(!m_Reflections);
		changed |= ImGui::Checkbox("屏幕外反射", &m_Settings.TraceScene);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("开启 SSR 后，若在画面里找不到倒影，就沿反射方向查找场景里的模型。");
		if (m_Settings.TraceScene) changed |= ImGui::SliderInt("镜面反射次数", &m_Settings.ReflectionBounces, 1, 4);
		if (m_Settings.TraceScene && ImGui::IsItemHovered()) ImGui::SetTooltip("1 次可照见物体，2 次可照见第二面镜子里的物体。每多一次，显卡最多再追一段光路。");
		changed |= ImGui::Checkbox("反射降噪", &m_Settings.DenoiseReflections);
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("开启 SSR 后，可用同一表面附近的反射颜色减轻颗粒。完全光滑的镜面保留原图。");
		changed |= ImGui::Checkbox("镜面 IBL", &m_Settings.SpecularEnvironment);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("按材质的粗糙度反射场景环境。启用 SSR 时，能在屏幕上找到的物体会出现在倒影中。");
		if (!m_Environment && !m_EnvironmentPath.empty()) ImGui::TextWrapped("环境文件读取失败，请重新选择。");
		changed |= ImGui::Checkbox("体积雾", &m_FogEnabled);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("点光源照亮空气里的雾，墙面能挡住穿过雾的光。");
		changed |= ImGui::Checkbox("TAA", &m_TAA);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("合并最近几帧的采样，减轻物体边缘的锯齿和闪烁。");
		return changed;
	}

	void LightingPreview::DrawEffectsMenu()
	{
		if (ImGui::Button("实时效果")) ImGui::OpenPopup("ViewportEffects");
		ImGui::SetNextWindowSizeConstraints({ ImGui::GetFontSize() * 13, 0 }, { FLT_MAX, FLT_MAX });
		if (!ImGui::BeginPopup("ViewportEffects")) return;
		ImGui::TextDisabled("组合光照设置");
		if (Mode != Shading::Lit)
			ImGui::TextWrapped("当前在单项视图。更改效果将切换到组合光照。");
		if (DrawLitOptions())
		{
			Mode = Shading::Lit;
			CompareDirect = false;
			ResetHistory();
		}
		ImGui::Separator();
		if (ImGui::Checkbox("对照：只显示直接光", &CompareDirect))
		{
			Mode = Shading::Lit;
			ResetHistory();
		}
		ImGui::EndPopup();
	}

	void LightingPreview::DrawSettings(bool allowBake, bool scenePreview)
	{
		int mode = int(Mode);
		ImGui::TextUnformatted("显示模式");
		ImGui::SetNextItemWidth(-1);
		if (ImGui::Combo("##Shading", &mode, s_Labels, IM_ARRAYSIZE(s_Labels), IM_ARRAYSIZE(s_Labels)))
		{
			Mode = Shading(mode);
			CompareDirect = false;
			ResetHistory();
		}
		if (Mode == Shading::BaseColor)
		{
			ImGui::TextWrapped("查看网格的底色和贴图。可在视口的「实时效果」中启用光照。");
			return;
		}
		bool changed = false;
		if (Mode == Shading::Lit && ImGui::CollapsingHeader("实时效果", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (DrawLitOptions()) { CompareDirect = false; changed = true; }
			changed |= ImGui::Checkbox("对照：只显示直接光", &CompareDirect);
		}
		if (ImGui::CollapsingHeader("质量与参数", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .50f);
			const int widths[] = { 0, 320, 480, 720, 960 };
			if (scenePreview && ImGui::BeginCombo("分辨率", m_PreviewWidth ? std::to_string(m_PreviewWidth).c_str() : "原始 Native"))
			{
				for (int w : widths)
					if (ImGui::Selectable(w ? std::to_string(w).c_str() : "原始 Native", w == m_PreviewWidth)) m_PreviewWidth = w;
				ImGui::EndCombo();
			}
			changed |= ImGui::Checkbox("SSAA 2×2", &m_Supersampling);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("每个像素取四个位置，分别算光照再平均。细线更平滑，但需要更多显存和绘制时间。");
			if (m_Supersampling && m_SampleSide == 1 && m_Width > 0)
				ImGui::TextWrapped(scenePreview ? "要使用四个样点，请降低上面的分辨率。" : "要使用四个样点，请降低主相机 Game 的游戏分辨率。");
			if (Mode == Shading::Lit)
			{
				int curve = m_Filmic ? 1 : 0;
				if (ImGui::Combo("色调映射", &curve, "Reinhard\0ACES 拟合\0"))
				{
					m_Filmic = curve == 1;
					m_StillFrames = 0;
				}
				if (ImGui::SliderFloat("曝光补偿 (EV)", &m_ExposureEV, -4.f, 4.f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
					m_StillFrames = 0;
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("增加 1 EV，显示前的亮度加倍。灯和材质的数值保持原样。");
				ImGui::Checkbox("移动时仅算直接光", &FastNavigation);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("拖动时保留点光源照明，停手后恢复间接光和其他已选效果。");
				if (m_FogEnabled)
				{
					changed |= ImGui::SliderFloat("雾密度", &m_Fog.Density, 0.f, .5f, "%.3f");
					changed |= ImGui::SliderFloat("散射比例", &m_Fog.Albedo, 0.f, 1.f);
					changed |= ImGui::SliderFloat("散射方向", &m_Fog.Anisotropy, -.8f, .8f);
					changed |= ImGui::SliderFloat("雾的最远距离", &m_Fog.Distance, 1.f, 100.f);
					changed |= ImGui::SliderInt("雾采样步数", &m_Fog.Steps, 4, 48);
				}
			}
			const bool showProbes = Mode == Shading::DDGI || (Mode == Shading::Lit && m_IndirectLight == ProbeLighting);
			const bool baked = m_GI && m_GI->HasBakedProbes();
			if (Mode == Shading::Direct || (showProbes && !baked))
				changed |= ImGui::SliderInt("采样数", &m_Samples, 1, 8);
			if (showProbes)
			{
				changed |= ImGui::Checkbox("多次漫反射", &m_ProbeFeedback);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("开启后，上轮到达墙面的反光可以再照亮邻近表面。烘焙会从第一轮重新计算。");
				if (!m_GI) ImGui::TextUnformatted("DDGI 探针：等待场景模型");
				else ImGui::TextUnformatted(baked ? "DDGI 探针：已烘焙" : "DDGI 探针：实时更新");
				bool canBake = allowBake && !m_ProbeFile.empty() && m_GI;
				ImGui::BeginDisabled(!canBake);
				const auto& io = ImGui::GetIO();
				bool shortcut = io.KeyCtrl && io.KeyShift && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_B, false);
				if ((ImGui::Button(baked ? "重新烘焙探针" : "烘焙探针") || shortcut) && canBake)
					m_BakeRequested = true;
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				{
					if (!allowBake) ImGui::SetTooltip("停止模拟后，可以烘焙编辑场景的光照。");
					else if (!m_GI) ImGui::SetTooltip("场景中需要有可绘制的模型。");
					else if (m_ProbeFile.empty()) ImGui::SetTooltip("先保存场景，再烘焙探针。");
					else ImGui::SetTooltip("保存当前场景的间接光。下次打开场景时可继续使用。Ctrl+Shift+B");
				}
				if (!m_ProbeMessage.empty()) ImGui::TextWrapped("%s", m_ProbeMessage.c_str());
			}
			if (Mode == Shading::GTAO || (Mode == Shading::Lit && m_AO && m_IndirectLight != NoIndirectLight))
			{
				changed |= ImGui::SliderFloat("遮蔽半径", &m_Settings.AORadius, .05f, 3.f);
				changed |= ImGui::SliderFloat("遮蔽强度", &m_Settings.AOIntensity, .05f, 3.f);
			}
			if (Mode == Shading::Clustered || (Mode == Shading::Lit && m_DirectLight == ClusteredLights))
				ImGui::TextDisabled("在点光源的属性中调整影响半径。");
			if (Mode == Shading::Reflections || (Mode == Shading::Lit && m_Reflections))
			{
				if (Mode == Shading::Reflections)
				{
					changed |= ImGui::Checkbox("屏幕外反射", &m_Settings.TraceScene);
					if (m_Settings.TraceScene) changed |= ImGui::SliderInt("镜面反射次数", &m_Settings.ReflectionBounces, 1, 4);
					changed |= ImGui::Checkbox("反射降噪", &m_Settings.DenoiseReflections);
					changed |= ImGui::SliderFloat("示例粗糙度", &m_Settings.Roughness, 0.f, 1.f);
				}
				changed |= ImGui::SliderFloat("反射距离", &m_Settings.ReflectionDistance, .1f, 30.f);
			}
			if (Mode == Shading::TAA || (Mode == Shading::Lit && m_TAA))
			{
				changed |= ImGui::SliderFloat("历史权重", &m_Settings.HistoryWeight, 0.f, .98f);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("设为 0 只显示当前帧。开启后，镜头停住时按细线和背景各被采到的次数混色。\n其他像素保留的旧颜色最多占这个比例。");
			}
			ImGui::PopItemWidth();
			if (Mode == Shading::DDGI || Mode == Shading::ReSTIRDI || Mode == Shading::ReSTIRGI || Mode == Shading::TAA || Mode == Shading::Lit)
			{
				if (ImGui::Button("重新积累")) ResetHistory();
				ImGui::SameLine();
				ImGui::TextDisabled("已采 %u 帧", m_Frame);
			}
			if (m_GI) ImGui::TextDisabled("光照图  %u × %u", m_Width, m_Height);
		}
		if (changed) ResetHistory();
	}

	static std::filesystem::path SettingsPath(std::filesystem::path scene) { return scene += ".render.yaml"; }

	void LightingPreview::Load(const std::filesystem::path& scene)
	{
		m_GI.reset();
		m_EnvironmentPath.clear();
		m_Environment.reset();
		m_Panorama = {};
		m_EnvironmentKey = 0;
		m_EnvironmentTime = {};
		m_ProbeFile = scene.empty() ? scene : std::filesystem::path(scene).concat(".rfprobes");
		m_ProbeMessage.clear();
		m_BakeRequested = false;
		ResetHistory();
		m_ProbeFeedback = true;
		Mode = scene.empty() ? Shading::Lit : Shading::BaseColor;
		CompareDirect = false;
		m_Settings = {};
		m_FogEnabled = false;
		m_Fog = {};
		m_Samples = 1;
		m_ShadowSize = 512;
		m_CascadeCount = 0;
		m_CascadeSize = 1024;
		m_ShadowDistance = 40.f;
		m_SunRadius = .27f;
		m_PreviewWidth = 0;
		m_Supersampling = false;
		m_Filmic = scene.empty();
		m_ExposureEV = 0.f;
		FastNavigation = false;
		m_DirectLight = AllLights;
		m_IndirectLight = scene.empty() ? EnvironmentDiffuse : ProbeLighting;
		m_AO = true;
		m_Reflections = m_TAA = false;
		if (scene.empty() || !std::filesystem::exists(SettingsPath(scene))) return;
		auto data = YAML::LoadFile(SettingsPath(scene).string());
		std::string name = data["Shading"].as<std::string>("");
		for (int i = 0; i < IM_ARRAYSIZE(s_Modes); ++i)
			if (name == s_Modes[i]) Mode = Shading(i);
		m_PreviewWidth = std::clamp(data["PreviewWidth"].as<int>(0), 0, 4096);
		m_Supersampling = data["Supersampling"].as<bool>(false);
		m_Filmic = data["ToneMapping"].as<std::string>("Reinhard") == "ACES";
		m_ExposureEV = std::clamp(data["ExposureEV"].as<float>(0.f), -4.f, 4.f);
		m_Samples = std::clamp(data["Samples"].as<int>(1), 1, 8);
		m_ShadowSize = std::clamp(data["AnimatedShadowSize"].as<int>(512), 0, 2048);
		m_CascadeCount = std::clamp(data["ShadowCascades"].as<int>(0), 0, 4);
		m_CascadeSize = std::clamp(data["CascadeResolution"].as<int>(1024), 128, 2048);
		m_ShadowDistance = std::clamp(data["ShadowDistance"].as<float>(40.f), 1.f, 250.f);
		m_SunRadius = std::clamp(data["SunRadiusDegrees"].as<float>(.27f), 0.f, 5.f);
		m_ProbeFeedback = data["DDGIProbeFeedback"].as<bool>(true);
		FastNavigation = data["FastNavigation"].as<bool>(FastNavigation);
		m_Settings.AORadius = data["AORadius"].as<float>(m_Settings.AORadius);
		m_Settings.AOIntensity = data["AOIntensity"].as<float>(m_Settings.AOIntensity);
		m_Settings.Roughness = data["Roughness"].as<float>(m_Settings.Roughness);
		m_Settings.ReflectionDistance = data["ReflectionDistance"].as<float>(m_Settings.ReflectionDistance);
		m_Settings.HistoryWeight = data["HistoryWeight"].as<float>(m_Settings.HistoryWeight);
		auto direct = data["Direct"].as<std::string>(s_Direct[0]);
		auto indirect = data["Indirect"].as<std::string>(s_Indirect[1]);
		for (int i = 0; i < IM_ARRAYSIZE(s_Direct); ++i) if (direct == s_Direct[i]) m_DirectLight = i;
		for (int i = 0; i < IM_ARRAYSIZE(s_Indirect); ++i) if (indirect == s_Indirect[i]) m_IndirectLight = i;
		m_AO = data["GTAO"].as<bool>(true);
		m_Reflections = data["SSR"].as<bool>(false);
		m_Settings.TraceScene = data["SceneReflections"].as<bool>(false);
		m_Settings.ReflectionBounces = std::clamp(data["ReflectionBounces"].as<int>(1), 1, 4);
		m_Settings.DenoiseReflections = data["DenoiseReflections"].as<bool>(false);
		m_Settings.SpecularEnvironment = data["SpecularIBL"].as<bool>(true);
		m_TAA = data["TAA"].as<bool>(false);
		m_FogEnabled = data["VolumetricFog"].as<bool>(false);
		m_Fog.Density = std::clamp(data["FogDensity"].as<float>(m_Fog.Density), 0.f, .5f);
		m_Fog.Albedo = std::clamp(data["FogAlbedo"].as<float>(m_Fog.Albedo), 0.f, 1.f);
		m_Fog.Anisotropy = std::clamp(data["FogAnisotropy"].as<float>(m_Fog.Anisotropy), -.8f, .8f);
		m_Fog.Distance = std::clamp(data["FogDistance"].as<float>(m_Fog.Distance), 1.f, 100.f);
		m_Fog.Steps = std::clamp(data["FogSteps"].as<int>(m_Fog.Steps), 4, 48);
	}

	void LightingPreview::Save(const std::filesystem::path& path, Scene& scene)
	{
		YAML::Node data;
		data["Shading"] = s_Modes[int(Mode)];
		data["PreviewWidth"] = m_PreviewWidth;
		data["Supersampling"] = m_Supersampling;
		data["ToneMapping"] = m_Filmic ? "ACES" : "Reinhard";
		data["ExposureEV"] = m_ExposureEV;
		data["Samples"] = m_Samples;
		data["AnimatedShadowSize"] = m_ShadowSize;
		data["ShadowCascades"] = m_CascadeCount;
		data["CascadeResolution"] = m_CascadeSize;
		data["ShadowDistance"] = m_ShadowDistance;
		data["SunRadiusDegrees"] = m_SunRadius;
		data["DDGIProbeFeedback"] = m_ProbeFeedback;
		data["FastNavigation"] = FastNavigation;
		data["AORadius"] = m_Settings.AORadius;
		data["AOIntensity"] = m_Settings.AOIntensity;
		data["Roughness"] = m_Settings.Roughness;
		data["ReflectionDistance"] = m_Settings.ReflectionDistance;
		data["HistoryWeight"] = m_Settings.HistoryWeight;
		data["Direct"] = s_Direct[m_DirectLight];
		data["Indirect"] = s_Indirect[m_IndirectLight];
		data["GTAO"] = m_AO;
		data["SSR"] = m_Reflections;
		data["SceneReflections"] = m_Settings.TraceScene;
		data["ReflectionBounces"] = m_Settings.ReflectionBounces;
		data["DenoiseReflections"] = m_Settings.DenoiseReflections;
		data["SpecularIBL"] = m_Settings.SpecularEnvironment;
		data["TAA"] = m_TAA;
		data["VolumetricFog"] = m_FogEnabled;
		data["FogDensity"] = m_Fog.Density;
		data["FogAlbedo"] = m_Fog.Albedo;
		data["FogAnisotropy"] = m_Fog.Anisotropy;
		data["FogDistance"] = m_Fog.Distance;
		data["FogSteps"] = m_Fog.Steps;
		FileSystem::WriteText(SettingsPath(path), YAML::Dump(data));

		auto probeFile = std::filesystem::path(path).concat(".rfprobes");
		if (probeFile != m_ProbeFile && !m_ProbeFile.empty() && std::filesystem::is_regular_file(m_ProbeFile))
		{
			// 另存为时重新读取天空，即使同路径的图片保留了原来的修改时间，也要核对新像素。
			RefreshEnvironment(scene.GetEnvironment(), true);
			const SceneGeometry geometry = CollectModels(scene);
			if (!geometry.Models.empty() && !MissingBakeModel(scene) && (m_Environment || m_EnvironmentPath.empty()))
			{
				Scope<GlobalIllumination> lighting = CreateProbeLighting(geometry.Models, CollectLights(scene));
				if (lighting->LoadProbes(m_ProbeFile))
					lighting->SaveProbes(probeFile);
			}
		}
		m_ProbeFile = probeFile;
	}
}
