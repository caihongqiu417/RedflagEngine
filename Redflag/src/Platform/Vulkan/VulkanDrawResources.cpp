#include "rfpch.h"
#include "VulkanDrawData.h"
#include "Redflag/Renderer/Mesh.h"

namespace Redflag::Vulkan {

    static VkDeviceMemory Allocate(VulkanContext& context, VkMemoryRequirements requirements,
        VkMemoryPropertyFlags properties, VkMemoryPropertyFlags preferred = 0, bool* canMap = nullptr)
    {
        VkPhysicalDeviceMemoryProperties memory;
        vkGetPhysicalDeviceMemoryProperties(context.GetPhysicalDevice(), &memory);
        uint32_t chosen = UINT32_MAX;
        std::pair<VkDeviceSize, bool> best{};
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        {
            const auto& type = memory.memoryTypes[i];
            if (!(requirements.memoryTypeBits & (1u << i)) || (type.propertyFlags & properties) != properties)
                continue;

            // 给光照数组选显存时先比较内存堆容量，避免挤进很小的映射区。容量相同才优先选 CPU 也能写的类型。
            auto rank = std::make_pair(preferred ? memory.memoryHeaps[type.heapIndex].size : VkDeviceSize(0),
                (type.propertyFlags & preferred) == preferred);
            if (chosen == UINT32_MAX || rank > best)
            {
                chosen = i;
                best = rank;
            }
        }
        if (chosen == UINT32_MAX)
            throw std::runtime_error("No compatible Vulkan memory type");
        if (canMap)
        {
            const auto mapped = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            *canMap = (memory.memoryTypes[chosen].propertyFlags & mapped) == mapped;
        }
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = chosen;
        VkDeviceMemory result{};
        Check(vkAllocateMemory(context.GetDevice(), &allocate, nullptr, &result), "vkAllocateMemory");
        return result;
    }

    Buffer::~Buffer()
    {
        vkDestroyBuffer(Device, Handle, nullptr);
        vkFreeMemory(Device, Memory, nullptr);
    }

    void* Buffer::Map()
    {
        void* mapped;
        Check(vkMapMemory(Device, Memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
        return mapped;
    }

    Scope<Buffer> CreateBuffer(VulkanContext& context, VkDeviceSize size,
        VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkMemoryPropertyFlags preferred)
    {
        auto result = CreateScope<Buffer>();
        result->Device = context.GetDevice();
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        Check(vkCreateBuffer(result->Device, &info, nullptr, &result->Handle), "vkCreateBuffer");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(result->Device, result->Handle, &requirements);
        result->Memory = Allocate(context, requirements, properties, preferred, &result->CanMap);
        Check(vkBindBufferMemory(result->Device, result->Handle, result->Memory, 0), "vkBindBufferMemory");
        return result;
    }

    Image::~Image()
    {
        vkDestroyImageView(Device, View, nullptr);
        vkDestroyImage(Device, Handle, nullptr);
        vkFreeMemory(Device, Memory, nullptr);
    }

    Scope<Image> CreateImage(VulkanContext& context, VkExtent2D extent, VkFormat format,
        VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t levels)
    {
        auto result = CreateScope<Image>();
        result->Device = context.GetDevice();
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {extent.width, extent.height, 1};
        info.mipLevels = levels;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        Check(vkCreateImage(result->Device, &info, nullptr, &result->Handle), "vkCreateImage");
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(result->Device, result->Handle, &requirements);
        result->Memory = Allocate(context, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Check(vkBindImageMemory(result->Device, result->Handle, result->Memory, 0), "vkBindImageMemory");

        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = result->Handle;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {aspect, 0, levels, 0, 1};
        Check(vkCreateImageView(result->Device, &view, nullptr, &result->View), "vkCreateImageView");
        return result;
    }

    TexturePool::~TexturePool() { vkDestroyDescriptorPool(Device, Pool, nullptr); }

    MaterialImage::~MaterialImage()
    {
        if (Set)
        {
            vkFreeDescriptorSets(Pool->Device, Pool->Pool, 1, &Set);
            --Pool->Used;
        }
    }

    SceneTarget::~SceneTarget() { vkDestroyFramebuffer(Device, Framebuffer, nullptr); }
}

namespace Redflag {
    using Vulkan::Check;

    // ---- 顶点与索引 ----
    Ref<MeshGeometry> VulkanDraw::CreateMesh(const std::vector<MeshVertex>& vertices,
        const std::vector<uint32_t>& indices)
    {
        if (vertices.empty() || indices.empty())
            throw std::invalid_argument("An indexed mesh requires vertices and indices");
        auto& context = m_Data->Context;
        const VkDeviceSize vertexBytes = vertices.size() * sizeof(MeshVertex);
        const VkDeviceSize indexBytes = indices.size() * sizeof(uint32_t);
        auto staging = Vulkan::CreateBuffer(context, vertexBytes + indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        auto bytes = static_cast<uint8_t*>(staging->Map());
        memcpy(bytes, vertices.data(), vertexBytes);
        memcpy(bytes + vertexBytes, indices.data(), indexBytes);
        staging->Unmap();

        auto mesh = CreateRef<Vulkan::Mesh>();
        mesh->IndexCount = static_cast<uint32_t>(indices.size());
        mesh->Vertices = Vulkan::CreateBuffer(context, vertexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        mesh->Indices = Vulkan::CreateBuffer(context, indexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        context.ImmediateSubmit([&](VkCommandBuffer cmd) {
            VkBufferCopy vertexRange{0, 0, vertexBytes}, indexRange{vertexBytes, 0, indexBytes};
            vkCmdCopyBuffer(cmd, staging->Handle, mesh->Vertices->Handle, 1, &vertexRange);
            vkCmdCopyBuffer(cmd, staging->Handle, mesh->Indices->Handle, 1, &indexRange);
            VkMemoryBarrier ready{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            ready.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            ready.dstAccessMask = VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                0, 1, &ready, 0, nullptr, 0, nullptr);
        });
        return mesh;
    }

    // ---- 材质贴图 ----
    static Vulkan::Texture::Channel ClassifyChannel(const uint8_t* rgba, size_t count, size_t channel)
    {
        using Channel = Vulkan::Texture::Channel;
        Channel result = Channel::White;
        for (size_t i = 0; i < count; ++i)
        {
            uint8_t value = rgba[4 * i + channel];
            if (value == 0) result = Channel::Binary;
            else if (value != 255) return Channel::Grayscale;
        }
        return result;
    }

    Ref<SampledTexture> VulkanDraw::CreateTexture(uint32_t width, uint32_t height, const void* rgba8)
    {
        auto texture = CreateRef<Vulkan::Texture>();
        texture->Extent = {width, height};
        const uint8_t* pixels = static_cast<const uint8_t*>(rgba8);
        texture->Alpha = ClassifyChannel(pixels, size_t(width) * height, 3);
        texture->Red = ClassifyChannel(pixels, size_t(width) * height, 0);
        const VkDeviceSize bytes = VkDeviceSize(width) * height * 4;
        texture->Staging = Vulkan::CreateBuffer(m_Data->Context, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        memcpy(texture->Staging->Map(), rgba8, bytes);
        texture->Staging->Unmap();
        return texture;
    }

    namespace
    {
        struct TextureUse
        {
            VkImageLayout Layout;
            VkPipelineStageFlags Stage;
            VkAccessFlags Access;
        };
        constexpr TextureUse Unused{VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0};
        constexpr TextureUse CopyTo{VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT};
        constexpr TextureUse CopyFrom{VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT};
        constexpr TextureUse Sample{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT};

        void Transition(VkCommandBuffer cmd, VkImage image, uint32_t mip, uint32_t count, TextureUse from, TextureUse to)
        {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.oldLayout = from.Layout;
            barrier.newLayout = to.Layout;
            barrier.srcAccessMask = from.Access;
            barrier.dstAccessMask = to.Access;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, count, 0, 1};
            vkCmdPipelineBarrier(cmd, from.Stage, to.Stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        void BuildMips(VkCommandBuffer cmd, VkImage image, VkExtent2D extent, uint32_t levels)
        {
            VkOffset3D size{int32_t(extent.width), int32_t(extent.height), 1};
            for (uint32_t level = 1; level < levels; ++level)
            {
                Transition(cmd, image, level - 1, 1, CopyTo, CopyFrom);
                VkOffset3D next{std::max(size.x / 2, 1), std::max(size.y / 2, 1), 1};
                VkImageBlit blit{};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
                blit.srcOffsets[1] = size;
                blit.dstOffsets[1] = next;
                // SRGB 格式的底色先在线性光下过滤，再编码存回；UNORM 格式的粗糙度等数据直接过滤。
                vkCmdBlitImage(cmd, image, CopyFrom.Layout, image, CopyTo.Layout, 1, &blit, VK_FILTER_LINEAR);
                Transition(cmd, image, level - 1, 1, CopyFrom, Sample);
                size = next;
            }
            Transition(cmd, image, levels - 1, 1, CopyTo, Sample);
        }
    }

    VkDescriptorSet VulkanDraw::GetPreviewTexture(const Ref<SampledTexture>& texture)
    {
        return m_Data->MaterialTexture(texture, VK_FORMAT_R8G8B8A8_UNORM);
    }

    VkDescriptorSet VulkanDraw::Data::MaterialTexture(const Ref<SampledTexture>& map, VkFormat format)
    {
        auto& texture = static_cast<Vulkan::Texture&>(*map);
        bool color = format == VK_FORMAT_R8G8B8A8_SRGB;
        auto& image = color ? texture.Color : texture.Data;
        if (image.Set) return image.Set;

        uint32_t levels = 1;
        for (auto size = std::max(texture.Extent.width,texture.Extent.height); size > 1; size >>= 1) ++levels;
        constexpr auto usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image.Pixels = Vulkan::CreateImage(Context, texture.Extent, format, usage, VK_IMAGE_ASPECT_COLOR_BIT, levels);
        const auto target = image.Pixels->Handle;
        const VkExtent3D extent{texture.Extent.width, texture.Extent.height, 1};

        Context.ImmediateSubmit([&](VkCommandBuffer cmd) {
            Transition(cmd, target, 0, levels, Unused, CopyTo);
            if (texture.Staging)
            {
                VkBufferImageCopy region{};
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageExtent = extent;
                vkCmdCopyBufferToImage(cmd, texture.Staging->Handle, target, CopyTo.Layout, 1, &region);
            }
            else
            {
                // 例如同一张图既作底色又作粗糙度图。显卡只复制原始分辨率的像素，再按新用途生成缩图。
                auto previous = (color ? texture.Data : texture.Color).Pixels->Handle;
                Transition(cmd, previous, 0, 1, Sample, CopyFrom);
                VkImageCopy copy{};
                copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.extent = extent;
                vkCmdCopyImage(cmd, previous, CopyFrom.Layout, target, CopyTo.Layout, 1, &copy);
                Transition(cmd, previous, 0, 1, CopyFrom, Sample);
            }
            BuildMips(cmd, target, texture.Extent, levels);
        });
        CreateTextureSet(image);
        texture.Staging.reset();
        return image.Set;
    }

    void VulkanDraw::Data::CreateTextureSet(Vulkan::MaterialImage& texture)
    {
        if (!TexturePool || TexturePool->Used == Vulkan::TexturePool::Capacity)
        {
            auto page = CreateRef<Vulkan::TexturePool>();
            page->Device = Device;
            page->TextureSampler = Sampler;
            VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, Vulkan::TexturePool::Capacity};
            VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool.maxSets = size.descriptorCount;
            pool.poolSizeCount = 1;
            pool.pPoolSizes = &size;
            Check(vkCreateDescriptorPool(Device, &pool, nullptr, &page->Pool), "vkCreateDescriptorPool");
            TexturePool = page;
        }
        texture.Pool = TexturePool;
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = TexturePool->Pool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &TextureLayout;
        VkDescriptorSet set{};
        Check(vkAllocateDescriptorSets(Device, &allocate, &set), "vkAllocateDescriptorSets");
        texture.Set = set;
        ++TexturePool->Used;

        VkDescriptorImageInfo image{Sampler->Handle, texture.Pixels->View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(Device, 1, &write, 0, nullptr);
    }
}
