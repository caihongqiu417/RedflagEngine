#pragma once

#include "Redflag/Renderer/GI/GlobalIllumination.h"
#include <functional>

namespace Redflag
{
    class CommandList;
    class VulkanContext;
    class VulkanDraw;

    /* 每个视口拥有各盏方向光的级联深度图。场景提供绘制回调，设备用下一层的光源投影重画同一批几何。 */
    class VulkanCascadeMaps
    {
    public:
        VulkanCascadeMaps(VulkanContext& context, ComputeDevice& device);
        ~VulkanCascadeMaps();

        Ref<ComputeResource> Draw(const glm::mat4& projection, const glm::mat4& view,
            const std::vector<GILight>& lights, glm::vec3 casterMin, glm::vec3 casterMax,
            unsigned count, unsigned resolution, float distance, float sourceRadiusDegrees,
            const std::function<void(CommandList&, const glm::mat4&)>& drawCasters);

    private:
        ComputeDevice& m_Device;
        Scope<VulkanDraw> m_DepthDraw;
        Ref<ComputePipeline> m_Store;
        Ref<ComputeResource> m_Maps;
        size_t m_Words = 0, m_MaxWords = 0;
    };
}
