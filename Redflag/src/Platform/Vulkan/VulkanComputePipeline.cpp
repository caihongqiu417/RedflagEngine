#include "rfpch.h"
#include "VulkanComputeData.h"

namespace Redflag {

    using Vulkan::Check;

    Vulkan::Kernel::~Kernel()
    {
        vkDestroyDescriptorPool(Device, Pool, nullptr);
        vkDestroyPipeline(Device, Pipeline, nullptr);
        vkDestroyPipelineLayout(Device, Layout, nullptr);
        vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
    }

    // ---- 程序与资源的对应关系 ----
    Ref<ComputePipeline> VulkanComputeDevice::CreatePipeline(const uint32_t* spirv, size_t spirvBytes,
        const std::vector<ComputeBinding>& bindings, uint32_t constantBytes)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(m_Context.GetPhysicalDevice(), &properties);
        if (!spirv || !spirvBytes || spirvBytes % 4 || constantBytes % 4 || constantBytes > properties.limits.maxPushConstantsSize)
            throw std::invalid_argument("Invalid SPIR-V or compute push constant size");
        auto kernel = CreateRef<Vulkan::Kernel>();
        kernel->Device = m_Context.GetDevice();
        kernel->Bindings = bindings;
        kernel->ConstantBytes = constantBytes;

        std::vector<VkDescriptorSetLayoutBinding> slots;
        uint32_t buffers = 0, images = 0;
        for (auto binding : bindings)
        {
            VkDescriptorSetLayoutBinding slot{};
            slot.binding = static_cast<uint32_t>(slots.size());
            slot.descriptorCount = 1;
            slot.descriptorType = binding == ComputeBinding::Buffer ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            slot.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            slots.push_back(slot);
            if (binding == ComputeBinding::Buffer) ++buffers;
            else ++images;
        }
        VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setInfo.bindingCount = static_cast<uint32_t>(slots.size());
        setInfo.pBindings = slots.data();
        Check(vkCreateDescriptorSetLayout(kernel->Device, &setInfo, nullptr, &kernel->SetLayout), "create compute set layout");

        VkPushConstantRange constants{ VK_SHADER_STAGE_COMPUTE_BIT, 0, constantBytes };
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &kernel->SetLayout;
        layout.pushConstantRangeCount = constantBytes ? 1 : 0;
        layout.pPushConstantRanges = constantBytes ? &constants : nullptr;
        Check(vkCreatePipelineLayout(kernel->Device, &layout, nullptr, &kernel->Layout), "create compute pipeline layout");

        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader.codeSize = spirvBytes;
        shader.pCode = spirv;
        VkShaderModule module{};
        Check(vkCreateShaderModule(kernel->Device, &shader, nullptr, &module), "create compute shader");
        VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline.stage.module = module;
        pipeline.stage.pName = "main";
        pipeline.layout = kernel->Layout;
        auto result = vkCreateComputePipelines(kernel->Device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &kernel->Pipeline);
        vkDestroyShaderModule(kernel->Device, module, nullptr);
        Check(result, "create compute pipeline");

        // 这条计算管线只需一组描述符。每次 Dispatch 都等显卡算完才返回，下次调用可以重新指定要读写的图和数组。
        std::vector<VkDescriptorPoolSize> sizes;
        if (buffers) sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffers});
        if (images) sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, images});
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
        pool.pPoolSizes = sizes.data();
        Check(vkCreateDescriptorPool(kernel->Device, &pool, nullptr, &kernel->Pool), "create compute descriptor pool");
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool = kernel->Pool;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &kernel->SetLayout;
        Check(vkAllocateDescriptorSets(kernel->Device, &allocation, &kernel->Set), "allocate compute descriptors");
        return kernel;
    }

    // ---- 提交计算与等待读写完成 ----
    void VulkanComputeDevice::Dispatch(const Ref<ComputePipeline>& pipeline,
        const std::vector<Ref<ComputeResource>>& resources, const void* constants, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        auto* kernel = dynamic_cast<Vulkan::Kernel*>(pipeline.get());
        if (!kernel || kernel->Device != m_Context.GetDevice() || resources.size() != kernel->Bindings.size()
            || (kernel->ConstantBytes && !constants))
            throw std::invalid_argument("Compute dispatch inputs do not match the pipeline");

        std::vector<VkWriteDescriptorSet> writes(resources.size());
        std::vector<VkDescriptorBufferInfo> buffers(resources.size());
        std::vector<VkDescriptorImageInfo> images(resources.size());
        for (size_t i = 0; i < resources.size(); ++i)
        {
            auto storage = dynamic_cast<Vulkan::Storage*>(resources[i].get());
            if (!storage || storage->Device != kernel->Device || bool(storage->Data) != (kernel->Bindings[i] == ComputeBinding::Buffer))
                throw std::invalid_argument("Compute binding has the wrong resource type or device");
            auto& write = writes[i];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = kernel->Set;
            write.dstBinding = static_cast<uint32_t>(i);
            write.descriptorCount = 1;
            if (storage->Data)
            {
                buffers[i] = {storage->Data->Handle, 0, storage->Bytes};
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                write.pBufferInfo = &buffers[i];
            }
            else
            {
                images[i] = {VK_NULL_HANDLE, storage->Pixels->View, VK_IMAGE_LAYOUT_GENERAL};
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                write.pImageInfo = &images[i];
            }
        }
        vkUpdateDescriptorSets(kernel->Device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        m_Context.ImmediateSubmit([&](VkCommandBuffer cmd) {
            // 如果上一帧的界面还在读取场景颜色，GPU 要等它读完才能改写这张图。这里也保证本次计算能读到先前计算或复制的结果。
            VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            before.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            before.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            auto producers = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
            vkCmdPipelineBarrier(cmd, producers, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, kernel->Pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, kernel->Layout, 0, 1, &kernel->Set, 0, nullptr);
            if (kernel->ConstantBytes)
                vkCmdPushConstants(cmd, kernel->Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, kernel->ConstantBytes, constants);
            vkCmdDispatch(cmd, groupsX, groupsY, groupsZ);

            VkMemoryBarrier ready{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            ready.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            ready.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, producers | VK_PIPELINE_STAGE_HOST_BIT,
                0, 1, &ready, 0, nullptr, 0, nullptr);
        });
    }
}
