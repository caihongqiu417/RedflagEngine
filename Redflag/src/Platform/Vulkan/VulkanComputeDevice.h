#pragma once

#include "Redflag/Renderer/RHI/ComputeDevice.h"
#include <vulkan/vulkan.h>

namespace Redflag {
    class VulkanContext;

    class VulkanComputeDevice final : public ComputeDevice
    {
    public:
        explicit VulkanComputeDevice(VulkanContext& context) : m_Context(context) {}

        Ref<ComputeResource> CreateBuffer(size_t bytes, const void* initial = nullptr) override;

        Ref<ComputeResource> CreateImage(uint32_t width, uint32_t height, const void* rgba32f = nullptr) override;

        void Upload(const Ref<ComputeResource>& resource, const void* src, size_t bytes) override;

        void Readback(const Ref<ComputeResource>& resource, void* dst, size_t bytes) override;

        VkImageView GetImageView(const Ref<ComputeResource>& resource) const;

        Ref<ComputePipeline> CreatePipeline(const uint32_t* spirv, size_t spirvBytes,
            const std::vector<ComputeBinding>& bindings, uint32_t constantBytes = 0) override;

        void Dispatch(const Ref<ComputePipeline>& pipeline, const std::vector<Ref<ComputeResource>>& resources,
            const void* constants, uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) override;

    private:
        VulkanContext& m_Context;
    };
}
