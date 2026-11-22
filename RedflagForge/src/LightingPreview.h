#pragma once

#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "Redflag/Renderer/GI/GlobalIllumination.h"
#include "Redflag/Renderer/ScreenSpace/ScreenEffects.h"
#include <vulkan/vulkan.h>

namespace Redflag
{
	class Scene;
	class VulkanContext;
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
			glm::vec3 eye, uint32_t viewportWidth, uint32_t viewportHeight, bool navigating);
		void ResetHistory();
		void DrawSettings(bool allowBake = true);
		void DrawEffectsMenu();
		void Load(const std::filesystem::path& scene);
		void Save(const std::filesystem::path& path, Scene& scene);
		void BakeProbes(Scene& scene);

		Shading Mode = Shading::BaseColor;
		bool FastNavigation = false;
		bool CompareDirect = false;
		static const char* ModeName(Shading mode);

	private:
		// 菜单按这个顺序列出选项，场景文件按名称保存。
		enum DirectLight { AllLights, ReSTIRDirect, ClusteredLights };
		enum IndirectLight { NoIndirectLight, ProbeLighting, ReSTIRIndirect, EnvironmentDiffuse };

		bool UsesFrameHistory(bool directOnly) const;
		bool DrawLitOptions();
		Ref<ComputeResource> RenderLit(const glm::mat4& projection, const glm::mat4& view,
			const glm::mat4& stableProjection, bool directOnly);

		VulkanContext& m_Context;
		VulkanComputeDevice m_Device;
		VkSampler m_Sampler;
		Ref<ComputeResource> CompositeTransparency(const Ref<ComputeResource>& background, const glm::mat4& vp);
		Ref<ComputeResource> AddEmission(const Ref<ComputeResource>& reflected);
		Scope<VulkanDraw> m_SurfaceDraw, m_TransparentDraw;
		Scope<VulkanDraw> m_EmissionDraw;
		Ref<ComputePipeline> m_AddEmission;
		Ref<ComputeResource> m_EmittedColor;
		Ref<ComputePipeline> m_CompositeTransparency;
		Ref<ComputeResource> m_TransparentColor;
		std::vector<glm::vec4> m_GlassSettings;
		std::vector<std::pair<Ref<SampledTexture>, bool>> m_SurfaceSettings;
		std::vector<std::pair<uint64_t, float>> m_LODSettings;
		std::vector<glm::mat4> m_Bones;
		Ref<ComputePipeline> m_PackSurfaces;
		Ref<ComputePipeline> m_Display;
		Ref<ComputePipeline> m_Combine;
		Ref<ComputeResource> m_CombinedLight;
		Ref<ComputeResource> m_Environment;
		std::filesystem::path m_EnvironmentPath;
		Ref<ComputeResource> m_Image;
		VkDescriptorSet m_Texture = VK_NULL_HANDLE;
		Scope<GlobalIllumination> m_GI;
		Scope<ScreenEffects> m_Effects;
		std::vector<GIModel> m_Models;
		ScreenEffectSettings m_Settings;
		Shading m_PreviousMode = Shading::BaseColor;
		glm::mat4 m_LastViewProjection{ 0 };
		uint32_t m_Width = 0, m_Height = 0, m_Frame = 0;
		uint32_t m_SampleSide = 1;
		VkExtent2D m_MaxExtent{};
		uint64_t m_MaxPixels = 0;
		bool m_Supersampling = false;
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
