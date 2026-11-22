#pragma once

#include "VulkanComputeDevice.h"
#include "VulkanDrawData.h"

namespace Redflag::Vulkan {

    struct Storage : ComputeResource
    {
        VkDevice Device{};
        size_t Bytes = 0;
        Scope<Buffer> Data;
        Scope<Image> Pixels;
        VkExtent2D Extent{};
    };

    struct Kernel : ComputePipeline
    {
        VkDevice Device{};
        VkDescriptorSetLayout SetLayout{};
        VkPipelineLayout Layout{};
        VkPipeline Pipeline{};
        VkDescriptorPool Pool{};
        VkDescriptorSet Set{};
        std::vector<ComputeBinding> Bindings;
        uint32_t ConstantBytes = 0;
        ~Kernel();
    };
}
