#pragma once

#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "Redflag/Renderer/EnvironmentLighting.h"
#include "Redflag/Renderer/DecalRenderer.h"
#include "Redflag/Renderer/GI/GlobalIllumination.h"
#include "Redflag/Renderer/ScreenSpace/ScreenEffects.h"
#include <vulkan/vulkan.h>
#include <tuple>

namespace Redflag
{
	// 实体、模型、配置版本、风开关、世界变换和颜色共同决定植被是否需要重画。
	using FoliageState = std::tuple<uint32_t, uint64_t, uint32_t, bool, glm::mat4, glm::vec4>;

	class Scene;
	class Mesh;
	class VulkanContext;
	class VulkanCascadeMaps;
	class VulkanDraw;
	struct SampledTexture;

	enum class Shading { BaseColor, Direct, DDGI, ReSTIRDI, ReSTIRGI, GTAO, Clustered, Reflections, TAA, Lit };

	/* 场景网格、相机和灯光来自编辑器。计算结果在显卡上转成一张可以贴到面板的纹理。 */
	class LightingPreview
	{
	public:
		LightingPreview(VulkanContext& context, VkSampler sampler);
		~LightingPreview();

		VkDescriptorSet Render(Scene& scene, const glm::mat4& projection, const glm::mat4& view,
			glm::vec3 eye, uint32_t viewportWidth, uint32_t viewportHeight, bool navigating, bool scenePreview = true);
		void ResetHistory();
		void DrawSettings(bool allowBake = true, bool scenePreview = true);
		void DrawEffectsMenu();
		void Load(const std::filesystem::path& scene);
		void Save(const std::filesystem::path& path, Scene& scene);
		void BakeProbes(Scene& scene);

		Shading Mode = Shading::BaseColor;
		bool FastNavigation = false;
		bool CompareDirect = false;
		bool NeedsModelOverlay() const { return m_OverlayModels; }
		std::vector<glm::u8vec4> ReadDisplayPixels();
		static const char* ModeName(Shading mode);

	private:
		// 菜单按这个顺序列出选项，场景文件按名称保存。
		enum DirectLight { AllLights, ReSTIRDirect, ClusteredLights };
		enum IndirectLight { NoIndirectLight, ProbeLighting, ReSTIRIndirect, EnvironmentDiffuse };

		bool UsesFrameHistory(bool directOnly) const;
		bool DrawLitOptions();
		bool RefreshEnvironment(const std::filesystem::path& path, bool readAgain = false);
		Scope<GlobalIllumination> CreateProbeLighting(const std::vector<GIModel>& models,
			const std::vector<GILight>& lights, glm::uvec2 size = { 1, 1 });
		Ref<ComputeResource> RenderLit(const glm::mat4& projection, const glm::mat4& view, bool directOnly);
		void DrawSky(const Ref<ComputeResource>& color, const glm::mat4& projection, const glm::mat4& view);
		void DrawAnimatedShadows(Scene& scene, const std::vector<GILight>& lights);
		void DrawCascadeShadows(Scene& scene, const std::vector<GILight>& lights, const glm::mat4& projection, const glm::mat4& view);
		VkDescriptorSet Display(const Ref<ComputeResource>& color);

		VulkanContext& m_Context;
		VulkanComputeDevice m_Device;
		DecalRenderer m_Decals{m_Device};
		VkSampler m_Sampler;
		Ref<ComputeResource> CompositeTransparency(const Ref<ComputeResource>& background, const glm::mat4& vp);
		Ref<ComputeResource> AddEmission(const Ref<ComputeResource>& reflected);
		Scope<VulkanDraw> m_SurfaceDraw, m_TransparentDraw;
		Scope<VulkanDraw> m_EmissionDraw;
		Scope<VulkanDraw> m_ShadowDraw;
		Scope<VulkanCascadeMaps> m_CascadeMaps;
		int m_CascadeCount = 0, m_CascadeSize = 1024;
		float m_ShadowDistance = 40.f, m_SunRadius = .27f;
		Ref<ComputePipeline> m_StoreShadow;
		Ref<ComputeResource> m_ShadowMaps;
		size_t m_ShadowWords = 0, m_MaxShadowWords = 0;
		int m_ShadowSize = 512;
		Ref<ComputePipeline> m_AddEmission;
		Ref<ComputeResource> m_EmittedColor;
		Ref<ComputePipeline> m_CompositeTransparency;
		Ref<ComputeResource> m_TransparentColor;
		std::vector<glm::vec4> m_GlassSettings;
		std::vector<std::tuple<Ref<SampledTexture>, bool, Ref<SampledTexture>, int, glm::vec2>> m_SurfaceSettings;
		std::vector<FoliageState> m_Foliage;
		std::vector<std::pair<uint64_t, float>> m_LODSettings;
		std::vector<glm::mat4> m_Bones;
		std::vector<Ref<Mesh>> m_AnimatedMeshes;
		Ref<ComputePipeline> m_PackSurfaces;
		Ref<ComputePipeline> m_Display;
		Ref<ComputePipeline> m_Sky;
		Ref<ComputePipeline> m_Combine;
		Ref<ComputeResource> m_CombinedLight;
		Ref<ComputeResource> m_Environment;
		EnvironmentLighting::Panorama m_Panorama;
		std::filesystem::path m_EnvironmentPath;
		std::filesystem::file_time_type m_EnvironmentTime{};
		uint64_t m_EnvironmentKey = 0;
		Ref<ComputeResource> m_Image;
		VkDescriptorSet m_Texture = VK_NULL_HANDLE;
		Scope<GlobalIllumination> m_GI;
		Scope<ScreenEffects> m_Effects;
		std::vector<GIModel> m_Models, m_Items;
		ScreenEffectSettings m_Settings;
		Shading m_PreviousMode = Shading::BaseColor;
		glm::mat4 m_LastViewProjection{ 0 };
		uint32_t m_Width = 0, m_Height = 0, m_Frame = 0;
		uint32_t m_SampleSide = 1;
		VkExtent2D m_MaxExtent{};
		uint64_t m_MaxPixels = 0;
		bool m_Supersampling = false;
		bool m_Filmic = false;
		float m_ExposureEV = 0.0f;
		bool m_SkyVisible = true;
		bool m_OverlayModels = false;
		uint32_t m_StillFrames = 0;
		int m_Samples = 1;
		int m_PreviewWidth = 0; // 0 表示按面板的实际像素绘制。
		int m_DirectLight = AllLights;
		int m_IndirectLight = ProbeLighting;
		bool m_AO = true, m_Reflections = false, m_TAA = false;
		bool m_FogEnabled = false;
		FogSettings m_Fog;
		std::filesystem::path m_ProbeFile;
		std::string m_ProbeMessage;
		bool m_BakeRequested = false;
		bool m_ProbeFeedback = true;
	};
}
