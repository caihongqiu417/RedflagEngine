#include "rfpch.h"
#include "VulkanComputeData.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/packing.hpp>

namespace Redflag {
    using Vulkan::Check;

    // ---- 面板资源 ----
    VulkanDraw::VulkanDraw(VulkanContext& context, Output output) : m_Data(CreateScope<Data>(context, output))
    {
        m_Data->CreatePass();
        m_Data->CreatePipeline();
        m_Data->IdentityInstance = Vulkan::CreateBuffer(context, sizeof(glm::mat4), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        const glm::mat4 identity(1.f);
        std::memcpy(m_Data->IdentityInstance->Map(), &identity, sizeof(identity));
        m_Data->IdentityInstance->Unmap();
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.minFilter = sampler.magFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.maxLod = VK_LOD_CLAMP_NONE;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        m_Data->Sampler = CreateRef<Vulkan::Sampler>();
        m_Data->Sampler->Device = context.GetDevice();
        Check(vkCreateSampler(context.GetDevice(), &sampler, nullptr, &m_Data->Sampler->Handle), "vkCreateSampler");
        const uint32_t white = 0xffffffff;
        m_Data->White = CreateTexture(1, 1, &white);
    }

    VulkanDraw::~VulkanDraw() = default;

    VulkanDraw::Data::~Data()
    {
        vkDeviceWaitIdle(Device);
        vkDestroyPipeline(Device, Pipeline, nullptr);
        vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        Target.reset();
        vkDestroyRenderPass(Device, Pass, nullptr);
        vkDestroyDescriptorSetLayout(Device, TextureLayout, nullptr);
        vkDestroyDescriptorSetLayout(Device, VertexTextureLayout, nullptr);
    }

    VkExtent2D VulkanDraw::GetExtent() const
    {
        return m_Data->Target ? m_Data->Target->Extent : VkExtent2D{};
    }
    VkImageView VulkanDraw::GetColorView() const { return m_Data->Target->Color->View; }
    VkSampler VulkanDraw::GetSampler() const { return m_Data->Sampler->Handle; }

    void VulkanDraw::SetSubpixelMask(bool enabled) { m_Data->SubpixelMask = enabled; }

    bool VulkanDraw::Resize(uint32_t width, uint32_t height)
    {
        return ResizeTarget(width, height, nullptr);
    }

    bool VulkanDraw::UseSceneDepth(const VulkanDraw& opaque)
    {
        m_Data->SubpixelMask = opaque.m_Data->SubpixelMask;
        const auto extent = opaque.GetExtent();
        return ResizeTarget(extent.width, extent.height, &opaque);
    }

    bool VulkanDraw::ResizeTarget(uint32_t width, uint32_t height, const VulkanDraw* opaque)
    {
        if (width == 0 || height == 0) return false;
        const bool transparent = m_Data->Mode == Output::Transparency;
        const bool emission = m_Data->Mode == Output::Emission;
        if ((transparent || emission) != (opaque != nullptr))
            throw std::logic_error("Transparency and emission must use the depth of the opaque scene");

        VkExtent2D previous = GetExtent();
        // 旧图和新图可能同宽同高；玻璃仍须使用本帧墙面写入的那张深度图。
        if (previous.width == width && previous.height == height &&
            (!opaque || m_Data->Target->Depth == opaque->m_Data->Target->Depth))
            return false;
        auto& context = m_Data->Context;
        auto target = CreateScope<Vulkan::SceneTarget>();
        target->Device = context.GetDevice();
        target->Extent = {width, height};
        VkImageView views[4]{};
        if (m_Data->Mode != Output::ColorAndEntity)
        {
            auto surface = [&](Ref<ComputeResource>& resource) {
                auto storage = CreateRef<Vulkan::Storage>();
                storage->Device = context.GetDevice();
                storage->Extent = {width, height};
                storage->Bytes = size_t(width) * height * sizeof(glm::vec4);
                storage->Pixels = Vulkan::CreateImage(context, storage->Extent, VK_FORMAT_R32G32B32A32_SFLOAT,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT);
                resource = storage;
                return storage->Pixels->View;
            };
            if (m_Data->Mode == Output::Emission)
                views[0] = surface(target->Emission);
            else
            {
                views[0] = surface(target->Albedo);
                views[1] = surface(target->NormalDepth);
                if (transparent) views[2] = surface(target->Optics);
            }
        }
        else
        {
            target->Color = Vulkan::CreateImage(context, target->Extent, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
            target->EntityIDs = Vulkan::CreateImage(context, target->Extent, VK_FORMAT_R32_SINT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            views[0] = target->Color->View;
            views[1] = target->EntityIDs->View;
        }
        if (opaque) target->Depth = opaque->m_Data->Target->Depth;
        else target->Depth = Vulkan::CreateImage(context, target->Extent, m_Data->DepthFormat,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        const uint32_t colors = m_Data->ColorCount();
        views[colors] = target->Depth->View;
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = m_Data->Pass;
        info.attachmentCount = colors + 1;
        info.pAttachments = views;
        info.width = width;
        info.height = height;
        info.layers = 1;
        Check(vkCreateFramebuffer(target->Device, &info, nullptr, &target->Framebuffer), "vkCreateFramebuffer(scene)");
        if (m_Data->Target)
            context.WaitIdle();
        m_Data->Target = std::move(target);
        return true;
    }

    // ---- 记录本次场景 ----
    void VulkanDraw::BeginScene(bool transparentBackground)
    {
        BeginScene(m_Data->Context.GetCommandBuffer(), transparentBackground);
    }

    void VulkanDraw::BeginScene(VkCommandBuffer cmd, bool transparentBackground)
    {
        m_Data->Recording = cmd;
        auto& frame = m_Data->Frames[m_Data->Context.GetFrameIndex()];
        frame.Meshes.clear();
        frame.Textures.clear();
        frame.UsedBatches = 0;
        auto& target = *m_Data->Target;
        m_Data->HasBlend = false;
        m_Data->HasEmission = false;
        const uint32_t colors = m_Data->ColorCount();
        VkClearValue clear[4]{};
        if (m_Data->Mode == Output::ColorAndEntity)
        {
            clear[0].color = {{0.1f, 0.1f, 0.1f, transparentBackground ? 0.0f : 1.0f}};
            clear[1].color.int32[0] = -1; // 背景处没有可选的实体。
        }
        clear[colors].depthStencil.depth = 1.0f;
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = m_Data->Pass;
        begin.framebuffer = target.Framebuffer;
        begin.renderArea.extent = target.Extent;
        begin.clearValueCount = colors + 1;
        begin.pClearValues = clear;
        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Data->Pipeline);

        // 项目相机里向上是正 Y。用负高度让它落在面板上方，左上角就是拾取时的第 0 行。
        float width = static_cast<float>(target.Extent.width);
        float height = static_cast<float>(target.Extent.height);
        VkViewport viewport{0, height, width, -height, 0, 1};
        VkRect2D scissor{{0, 0}, target.Extent};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }

    void VulkanDraw::DrawIndexed(const Ref<MeshGeometry>& geometry, const Ref<SampledTexture>& texture,
        const glm::mat4& viewProjection, const glm::mat4& transform, const glm::vec4& color, int entityID,
        const SurfaceMaterial& surface, const std::vector<glm::mat4>* instances)
    {
        auto& context = m_Data->Context;
        VkCommandBuffer cmd = m_Data->Recording;
        m_Data->HasBlend |= surface.Blend;
        const bool emission = m_Data->Mode == Output::Emission;
        const bool lighting = m_Data->Mode == Output::LightingSurfaces;
        const bool transparent = m_Data->Mode == Output::Transparency;
        if ((transparent && !surface.Blend) || ((lighting || emission) && surface.Blend)) return;
        const bool emitting = m_Data->Mode != Output::ColorAndEntity && glm::any(glm::greaterThan(surface.Emission, glm::vec3(0)));
        if (emission && !emitting) return;
        if (lighting && emitting) m_Data->HasEmission = true;
        const auto& sampled = texture ? texture : m_Data->White;
        const auto& mesh = static_cast<const Vulkan::Mesh&>(*geometry);
        const auto& normal = surface.Normal ? surface.Normal : m_Data->White;
        const bool heightMapped = surface.Height && surface.HeightMode != HeightMapping::Off &&
            surface.HeightScale > 0.f && surface.AlphaCutoff < 0.f && !surface.Blend;
        // 实心表面不需要透明度贴图，这个位置可供同一次绘制读取灰度高度。
        const auto& opacity = heightMapped ? surface.Height : (surface.Opacity ? surface.Opacity : m_Data->White);
        const auto& materialMap = surface.MetallicRoughnessMap ? surface.MetallicRoughnessMap : m_Data->White;
        const auto& emissionMap = emitting && surface.EmissiveMap ? surface.EmissiveMap : m_Data->White;
        VkDescriptorSet textures[] = {
            m_Data->MaterialTexture(sampled, VK_FORMAT_R8G8B8A8_SRGB),
            emission ? m_Data->MaterialTexture(emissionMap, VK_FORMAT_R8G8B8A8_SRGB)
                     : m_Data->MaterialTexture(normal, VK_FORMAT_R8G8B8A8_UNORM),
            m_Data->MaterialTexture(opacity, VK_FORMAT_R8G8B8A8_UNORM, true),
            lighting ? m_Data->MaterialTexture(materialMap, VK_FORMAT_R8G8B8A8_UNORM) : VK_NULL_HANDLE
        };
        auto& frame = m_Data->Frames[context.GetFrameIndex()];
        frame.Meshes.push_back(geometry);
        frame.Textures.push_back(sampled);
        if (!emission && surface.Normal) frame.Textures.push_back(normal);
        if ((emission || transparent) && emitting && surface.EmissiveMap)
            frame.Textures.push_back(emissionMap);
        if (heightMapped || surface.Opacity) frame.Textures.push_back(opacity);
        if (lighting && surface.MetallicRoughnessMap) frame.Textures.push_back(materialMap);

        VkBuffer matrices = m_Data->IdentityInstance->Handle;
        const uint32_t instanceCount = instances ? static_cast<uint32_t>(instances->size()) : 1u;
        if (!instanceCount) return;
        if (instances)
        {
            // 一帧中的不同草地各占一份矩阵。回到同一帧槽时，围栏已经确认上次绘制结束。
            if (frame.UsedBatches == frame.Batches.size()) frame.Batches.emplace_back();
            auto& batch = frame.Batches[frame.UsedBatches++];
            if (batch.Capacity < instanceCount)
            {
                batch.Buffer = Vulkan::CreateBuffer(context, instances->size() * sizeof(glm::mat4), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                batch.Capacity = instanceCount;
            }
            std::memcpy(batch.Buffer->Map(), instances->data(), instances->size() * sizeof(glm::mat4));
            batch.Buffer->Unmap();
            matrices = batch.Buffer->Handle;
        }
        const VkBuffer buffers[] = {mesh.Vertices->Handle, matrices};
        const VkDeviceSize offsets[] = {0, 0};
        vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
        vkCmdBindIndexBuffer(cmd, mesh.Indices->Handle, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Data->PipelineLayout,
            0, lighting ? 4u : 3u, textures, 0, nullptr);
        Vulkan::ObjectData object{viewProjection * transform, color, entityID};
        if (emission) object.Color = glm::vec4(surface.Emission, color.a);
        object.AlphaCutoff = surface.AlphaCutoff;
        object.MaterialFlags = (surface.Normal ? 1u : 0u) | ((surface.Blend || emission) && !surface.DoubleSided ? 2u : 0u);
        if (instances) object.MaterialFlags |= 64u;
        if (heightMapped)
            object.MaterialFlags |= (uint32_t(surface.HeightMode) << 7u) | glm::packHalf2x16({0.f, surface.HeightScale});
        if (m_Data->SubpixelMask && (lighting || emission) && color.a > 0.f && surface.AlphaCutoff == .5f * color.a)
        {
            using Channel = Vulkan::Texture::Channel;
            const Channel alpha = static_cast<const Vulkan::Texture&>(*sampled).Alpha;
            const Channel red = static_cast<const Vulkan::Texture&>(*opacity).Red;
            // 只有一张图决定孔的位置时，黑白图缩小后的灰度才表示铁丝占了多少面积。
            if (alpha == Channel::Binary && red == Channel::White) object.MaterialFlags |= 16u;
            if (red == Channel::Binary && alpha == Channel::White) object.MaterialFlags |= 32u;
        }
        if (lighting)
        {
            // 拾取时这 4 个字节存实体编号。画光照表面时改存金属度和粗糙度，各占 16 位。
            object.MaterialFactors = glm::packUnorm2x16({surface.Metallic, surface.Roughness});
            if (surface.MetallicRoughnessMap) object.MaterialFlags |= 4u;
            if (emitting) object.MaterialFlags |= 8u;
        }
        if (transparent)
        {
            object.RefractionIndex = surface.RefractionIndex;
            object.Thickness = surface.Thickness;
        }
        if (lighting || transparent)
        {
            const glm::mat3 normals = glm::transpose(glm::inverse(glm::mat3(transform)));
            std::memcpy(object.NormalMatrix, glm::value_ptr(normals), sizeof(object.NormalMatrix));
        }
        vkCmdPushConstants(cmd, m_Data->PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(object), &object);
        vkCmdDrawIndexed(cmd, mesh.IndexCount, instanceCount, 0, 0, 0);

        if (transparent && emitting)
        {
            // 发光沿用玻璃这层的覆盖率。再画一次只往 RGB 加光，已有的透过率和折射数据保持原值。
            const auto map = m_Data->MaterialTexture(emissionMap, VK_FORMAT_R8G8B8A8_SRGB);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Data->PipelineLayout, 1, 1, &map, 0, nullptr);
            object.Color = glm::vec4(surface.Emission, color.a);
            object.MaterialFlags |= 8u;
            vkCmdPushConstants(cmd, m_Data->PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(object), &object);
            vkCmdDrawIndexed(cmd, mesh.IndexCount, instanceCount, 0, 0, 0);
        }
    }

    void VulkanDraw::EndScene()
    {
        vkCmdEndRenderPass(m_Data->Recording);
        m_Data->Recording = VK_NULL_HANDLE;
    }

    void VulkanDraw::DrawSurfaces(const std::function<void(CommandList&)>& drawScene)
    {
        m_Data->Context.ImmediateSubmit([&](VkCommandBuffer cmd) {
            BeginScene(cmd);
            drawScene(*this);
            EndScene();
        }, VulkanContext::Work::Surfaces);
    }

    const Ref<ComputeResource>& VulkanDraw::GetAlbedo() const { return m_Data->Target->Albedo; }
    const Ref<ComputeResource>& VulkanDraw::GetNormalDepth() const { return m_Data->Target->NormalDepth; }
    bool VulkanDraw::HasEmission() const { return m_Data->HasEmission; }
    const Ref<ComputeResource>& VulkanDraw::GetEmission() const { return m_Data->Target->Emission; }

    bool VulkanDraw::HasTransparency() const { return m_Data->HasBlend; }
    const Ref<ComputeResource>& VulkanDraw::GetOptics() const { return m_Data->Target->Optics; }

    // ---- 颜色和实体编号读回 ----
    VulkanDraw::Pixel VulkanDraw::ReadPixel(uint32_t x, uint32_t y)
    {
        Pixel pixel;
        ReadRegion(x, y, 1, 1, &pixel.Color, &pixel.EntityID);
        return pixel;
    }

    void VulkanDraw::ReadRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height, glm::u8vec4* colors, int* entities)
    {
        auto& context = m_Data->Context;
        auto& target = *m_Data->Target;
        if (!width || !height || x >= target.Extent.width || y >= target.Extent.height ||
            width > target.Extent.width - x || height > target.Extent.height - y)
            throw std::out_of_range("Pixel region outside scene target");
        const size_t bytes = size_t(width) * height * 4;
        auto readback = Vulkan::CreateBuffer(context, 2 * bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        context.ImmediateSubmit([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier color{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            color.image = target.Color->Handle;
            color.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            color.srcQueueFamilyIndex = color.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            color.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            color.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            color.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            color.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &color);

            VkBufferImageCopy pixel{};
            pixel.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            pixel.imageOffset = {static_cast<int>(x), static_cast<int>(y), 0};
            pixel.imageExtent = {width, height, 1};
            vkCmdCopyImageToBuffer(cmd, target.Color->Handle, color.newLayout, readback->Handle, 1, &pixel);
            pixel.bufferOffset = bytes; // RGBA 像素之后，按相同的行序保存每个像素的实体编号。
            vkCmdCopyImageToBuffer(cmd, target.EntityIDs->Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                readback->Handle, 1, &pixel);

            color.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            color.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            color.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            color.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            VkMemoryBarrier cpu{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            cpu.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            cpu.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &cpu, 0, nullptr, 1, &color);
        });
        const auto* mapped = static_cast<const uint8_t*>(readback->Map());
        memcpy(colors, mapped, bytes);
        memcpy(entities, mapped + bytes, bytes);
        readback->Unmap();
    }
}
