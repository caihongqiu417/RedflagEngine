#include "rfpch.h"
#include "VulkanComputeData.h"

namespace Redflag {

    namespace {
        constexpr auto HostMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        constexpr auto ShaderStages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

        VkImageMemoryBarrier ImageBarrier(VkImage image, VkImageLayout from, VkImageLayout to,
            VkAccessFlags before, VkAccessFlags after)
        {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.image = image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.oldLayout = from;
            barrier.newLayout = to;
            barrier.srcAccessMask = before;
            barrier.dstAccessMask = after;
            return barrier;
        }

        Vulkan::Storage& GetStorage(const Ref<ComputeResource>& resource, VkDevice device)
        {
            auto* storage = dynamic_cast<Vulkan::Storage*>(resource.get());
            if (!storage || storage->Device != device)
                throw std::invalid_argument("Compute resource belongs to another device");
            return *storage;
        }

        void CopyArray(VulkanContext& context, Vulkan::Storage& storage, VkBuffer staging, bool upload)
        {
            context.ImmediateSubmit([&](VkCommandBuffer cmd) {
                VkMemoryBarrier previous{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                previous.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                previous.dstAccessMask = upload ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd, ShaderStages | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 1, &previous, 0, nullptr, 0, nullptr);

                VkBufferCopy bytes{0, 0, storage.Bytes};
                auto array = storage.Data->Handle;
                vkCmdCopyBuffer(cmd, upload ? staging : array, upload ? array : staging, 1, &bytes);

                VkMemoryBarrier ready{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                ready.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                ready.dstAccessMask = upload ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
                                             | VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_HOST_READ_BIT;
                auto next = upload ? ShaderStages | VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_HOST_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, next, 0, 1, &ready, 0, nullptr, 0, nullptr);
            });
        }

        // 上传时，显卡把暂存缓冲区里逐行排列的像素复制到二维图像。读回时方向相反，复制完 CPU 才能取色。
        void CopyPixels(VulkanContext& context, Vulkan::Storage& storage, VkBuffer staging, bool upload)
        {
            context.ImmediateSubmit([&](VkCommandBuffer cmd) {
                auto layout = upload ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                auto access = upload ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_TRANSFER_READ_BIT;
                auto begin = ImageBarrier(storage.Pixels->Handle, VK_IMAGE_LAYOUT_GENERAL, layout,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, access);
                vkCmdPipelineBarrier(cmd, ShaderStages, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &begin);

                VkBufferImageCopy range{};
                range.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                range.imageExtent = {storage.Extent.width, storage.Extent.height, 1};
                if (upload)
                    vkCmdCopyBufferToImage(cmd, staging, storage.Pixels->Handle, layout, 1, &range);
                else
                    vkCmdCopyImageToBuffer(cmd, storage.Pixels->Handle, layout, staging, 1, &range);

                auto end = ImageBarrier(storage.Pixels->Handle, layout, VK_IMAGE_LAYOUT_GENERAL, access,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                VkMemoryBarrier cpu{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                cpu.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                cpu.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, ShaderStages | VK_PIPELINE_STAGE_HOST_BIT, 0,
                    upload ? 0 : 1, upload ? nullptr : &cpu, 0, nullptr, 1, &end);
            });
        }
    }

    // ---- 创建计算着色器读写的缓冲区和图像 ----
    Ref<ComputeResource> VulkanComputeDevice::CreateBuffer(size_t bytes, const void* initial)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(m_Context.GetPhysicalDevice(), &props);
        if (!bytes || bytes > props.limits.maxStorageBufferRange)
            throw std::invalid_argument("Storage buffer size exceeds the device's descriptor range");
        auto storage = CreateRef<Vulkan::Storage>();
        storage->Device = m_Context.GetDevice();
        storage->Bytes = bytes;
        storage->Data = Vulkan::CreateBuffer(m_Context, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, HostMemory);
        if (initial)
            Upload(storage, initial, bytes);
        return storage;
    }

    Ref<ComputeResource> VulkanComputeDevice::CreateImage(uint32_t width, uint32_t height, const void* rgba32f)
    {
        VkFormatProperties features;
        vkGetPhysicalDeviceFormatProperties(m_Context.GetPhysicalDevice(), VK_FORMAT_R32G32B32A32_SFLOAT, &features);
        const auto required = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
            | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if ((features.optimalTilingFeatures & required) != required)
            throw std::runtime_error("RGBA32F storage and sampled images are unavailable on this device");
        if (!width || !height)
            throw std::invalid_argument("A storage image needs a nonempty extent");

        auto storage = CreateRef<Vulkan::Storage>();
        storage->Device = m_Context.GetDevice();
        storage->Extent = {width, height};
        storage->Bytes = size_t(width) * height * 4 * sizeof(float);
        storage->Pixels = Vulkan::CreateImage(m_Context, storage->Extent, VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        m_Context.ImmediateSubmit([&](VkCommandBuffer cmd) {
            auto ready = ImageBarrier(storage->Pixels->Handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, ShaderStages, 0, 0, nullptr, 0, nullptr, 1, &ready);
        });
        if (rgba32f)
            Upload(storage, rgba32f, storage->Bytes);
        return storage;
    }

    VkImageView VulkanComputeDevice::GetImageView(const Ref<ComputeResource>& resource) const
    {
        auto& storage = GetStorage(resource, m_Context.GetDevice());
        if (!storage.Pixels)
            throw std::invalid_argument("Only a storage image has an image view");
        return storage.Pixels->View;
    }

    // ---- CPU 与 GPU 间的数据传递 ----
    void VulkanComputeDevice::Upload(const Ref<ComputeResource>& resource, const void* src, size_t bytes)
    {
        auto& storage = GetStorage(resource, m_Context.GetDevice());
        if (!src || bytes != storage.Bytes)
            throw std::invalid_argument("Upload must cover the complete compute resource");
        if (storage.Data && storage.Data->CanMap)
        {
            memcpy(storage.Data->Map(), src, bytes);
            storage.Data->Unmap();
            return;
        }

        auto staging = Vulkan::CreateBuffer(m_Context, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, HostMemory);
        memcpy(staging->Map(), src, bytes);
        staging->Unmap();
        if (storage.Data)
            CopyArray(m_Context, storage, staging->Handle, true);
        else
            CopyPixels(m_Context, storage, staging->Handle, true);
    }

    void VulkanComputeDevice::Readback(const Ref<ComputeResource>& resource, void* dst, size_t bytes)
    {
        auto& storage = GetStorage(resource, m_Context.GetDevice());
        if (!dst || bytes != storage.Bytes)
            throw std::invalid_argument("Readback must cover the complete compute resource");
        if (storage.Data && storage.Data->CanMap)
        {
            memcpy(dst, storage.Data->Map(), bytes);
            storage.Data->Unmap();
            return;
        }

        auto staging = Vulkan::CreateBuffer(m_Context, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, HostMemory);
        if (storage.Data)
            CopyArray(m_Context, storage, staging->Handle, false);
        else
            CopyPixels(m_Context, storage, staging->Handle, false);
        memcpy(dst, staging->Map(), bytes);
        staging->Unmap();
    }
}
