#include "rfpch.h"
#include "VulkanCascadeMaps.h"
#include "VulkanDraw.h"
#include "VulkanContext.h"
#include "Redflag/Renderer/ShadowCascades.h"

namespace Redflag
{
    struct CascadeStore
    {
        glm::mat4 LightVP;
        glm::vec4 CameraDepth;
        uint32_t Lamp, First, Layer, Count;
        float FarDepth, Texel, DepthRange, SourceSlope;
    };
    static_assert(sizeof(CascadeStore) == 112);

    VulkanCascadeMaps::VulkanCascadeMaps(VulkanContext& context, ComputeDevice& device)
        : m_Device(device), m_DepthDraw(CreateScope<VulkanDraw>(context, VulkanDraw::Output::LightingSurfaces))
    {
        const uint32_t code[] =
#include "Shaders/StoreCascade.comp.inc"
            ;
        m_Store = device.CreatePipeline(code, sizeof(code), {ComputeBinding::Image, ComputeBinding::Buffer}, sizeof(CascadeStore));
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(context.GetPhysicalDevice(), &properties);
        m_MaxWords = properties.limits.maxStorageBufferRange / sizeof(uint32_t);
    }

    VulkanCascadeMaps::~VulkanCascadeMaps() = default;

    Ref<ComputeResource> VulkanCascadeMaps::Draw(const glm::mat4& projection, const glm::mat4& view,
        const std::vector<GILight>& lights, glm::vec3 casterMin, glm::vec3 casterMax,
        unsigned count, unsigned resolution, float distance, float sourceRadiusDegrees,
        const std::function<void(CommandList&, const glm::mat4&)>& drawCasters)
    {
        size_t lamps = std::count_if(lights.begin(), lights.end(), [](const GILight& lamp) { return lamp.Position.w == -1.f; });
        if (!lamps) return {};
        // 每盏开阴影的太阳有一组分层距离，之后每层分别存矩阵、像素尺寸、深度跨度和深度图。
        auto words = [&] { return lights.size() + lamps * (7u + count + count * (18u + size_t(resolution) * resolution)); };
        while (words() > m_MaxWords && resolution > 4) resolution /= 2;
        if (words() > m_MaxWords) throw std::runtime_error("Cascaded shadow maps exceed maxStorageBufferRange");
        if (words() != m_Words)
        {
            m_Words = words();
            m_Maps = m_Device.CreateBuffer(m_Words * sizeof(uint32_t));
        }
        m_DepthDraw->Resize(resolution, resolution);

        const glm::vec4 depth(-view[0][2], -view[1][2], -view[2][2], -view[3][2]);
        const float slope = std::tan(glm::radians(sourceRadiusDegrees));
        uint32_t first = uint32_t(lights.size());
        for (uint32_t i = 0; i < lights.size(); ++i)
        {
            if (lights[i].Position.w != -1.f) continue;
            const std::vector<ShadowCascade> layers = BuildShadowCascades(projection, view, -glm::vec3(lights[i].Position),
                casterMin, casterMax, count, distance, resolution);
            if (layers.empty()) return {};
            for (uint32_t layer = 0; layer < count; ++layer)
            {
                const ShadowCascade& cascade = layers[layer];
                m_DepthDraw->DrawSurfaces([&](CommandList& commands) { drawCasters(commands, cascade.ViewProjection); });
                CascadeStore store{cascade.ViewProjection, depth, i, first, layer, count,
                    cascade.FarDepth, cascade.TexelSize, cascade.DepthRange, slope};
                m_Device.Dispatch(m_Store, {m_DepthDraw->GetNormalDepth(), m_Maps}, &store,
                    (resolution + 7) / 8, (resolution + 7) / 8);
            }
            first += 7u + count + count*(18u + resolution*resolution);
        }
        return m_Maps;
    }
}
