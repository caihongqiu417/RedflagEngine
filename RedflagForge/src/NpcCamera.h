#pragma once

#include "Redflag/AI/NpcPhoto.h"
#include "LightingPreview.h"
#include "Platform/Vulkan/VulkanDraw.h"

namespace Redflag
{
	class NpcCamera
	{
	public:
		explicit NpcCamera(VulkanContext& context);
		NpcPhoto Take(Scene& scene, uint64_t observer, const std::filesystem::path& scenePath, bool includeObjects = true);

	private:
		VulkanDraw m_Objects;
		LightingPreview m_Lighting;
		std::filesystem::path m_Path;
	};
}
