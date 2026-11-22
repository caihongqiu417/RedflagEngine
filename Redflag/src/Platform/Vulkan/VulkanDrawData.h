#pragma once

#include "VulkanContext.h"
#include "VulkanDraw.h"
#include "VulkanResult.h"
#include "Redflag/Renderer/Mesh.h"
#include "Redflag/Renderer/Texture.h"

namespace Redflag::Vulkan {

    // 缓冲区和图像各自持有显存。创建中途失败时，也能归还已经取得的对象。
    struct Buffer
    {
        VkDevice Device{};
        VkBuffer Handle{};
        VkDeviceMemory Memory{};
        bool CanMap = false;
        ~Buffer();
        void* Map();
        void Unmap() { vkUnmapMemory(Device, Memory); }
    };

    struct Image
    {
        VkDevice Device{};
        VkImage Handle{};
        VkImageView View{};
        VkDeviceMemory Memory{};
        ~Image();
    };

    Scope<Buffer> CreateBuffer(VulkanContext& context, VkDeviceSize size,
        VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkMemoryPropertyFlags preferred = 0);
    Scope<Image> CreateImage(VulkanContext& context, VkExtent2D extent, VkFormat format,
        VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t levels = 1);

    struct Mesh : MeshGeometry
    {
        Scope<Buffer> Vertices, Indices;
        uint32_t IndexCount = 0;
    };

    struct Sampler
    {
        VkDevice Device{};
        VkSampler Handle{};
        ~Sampler() { vkDestroySampler(Device, Handle, nullptr); }
    };

    struct TexturePool
    {
        static constexpr uint32_t Capacity = 64;
        VkDevice Device{};
        VkDescriptorPool Pool{};
        Ref<Sampler> TextureSampler;
        uint32_t Used = 0;
        ~TexturePool();
    };

    struct TextureSet
    {
        Ref<TexturePool> Pool;
        VkDescriptorSet Set{};
        ~TextureSet();
    };

    struct MaterialImage : TextureSet
    {
        Scope<Image> Pixels;
        TextureSet VertexAndFragment;
    };

    struct Texture : SampledTexture
    {
        enum class Channel { White, Binary, Grayscale };
        VkExtent2D Extent{};
        Scope<Buffer> Staging;
        MaterialImage Color, Data;
        Channel Alpha, Red;
    };

    struct SceneTarget
    {
        VkDevice Device{};
        VkExtent2D Extent{};
        Scope<Image> Color, EntityIDs;
        Ref<Image> Depth;
        Ref<ComputeResource> Albedo, NormalDepth, Optics, Emission;
        VkFramebuffer Framebuffer{};
        ~SceneTarget();
    };

    struct ObjectData
    {
        glm::mat4 ViewProjectionModel;
        glm::vec4 Color;
        union { int EntityID; float RefractionIndex; uint32_t MaterialFactors; };
        float NormalMatrix[9];
        union { float AlphaCutoff; float Thickness; };
        uint32_t MaterialFlags;
    };
    static_assert(offsetof(ObjectData, Color) == 64 && offsetof(ObjectData, NormalMatrix) == 84 && sizeof(ObjectData) == 128);
}

namespace Redflag {
    struct VulkanDraw::Data
    {
        Data(VulkanContext& context, Output output) : Context(context), Device(context.GetDevice()), Mode(output) {}
        ~Data();
        void CreatePass();
        void CreatePipeline();
        VkDescriptorSet MaterialTexture(const Ref<SampledTexture>& texture, VkFormat format, bool vertex = false);
        void CreateTextureSet(Vulkan::TextureSet& binding, VkImageView image, VkDescriptorSetLayout layout);

        VulkanContext& Context;
        VkDevice Device;
        Output Mode;
        bool HasBlend = false;
        bool HasEmission = false;
        bool SubpixelMask = false;
        uint32_t ColorCount() const { return Mode == Output::Emission ? 1u : Mode == Output::Transparency ? 3u : 2u; }
        VkCommandBuffer Recording{};
        VkFormat DepthFormat{};
        VkRenderPass Pass{};
        VkDescriptorSetLayout TextureLayout{}, VertexTextureLayout{};
        VkPipelineLayout PipelineLayout{};
        VkPipeline Pipeline{};
        Ref<Vulkan::Sampler> Sampler;
        Ref<Vulkan::TexturePool> TexturePool;
        Ref<SampledTexture> White;
        Scope<Vulkan::Buffer> IdentityInstance;
        Scope<Vulkan::SceneTarget> Target;

        // 显卡还在画这一帧时，网格和贴图都得留着。等该帧的围栏发出完成信号，才可清掉这些引用。
        struct FrameResources
        {
            std::vector<Ref<MeshGeometry>> Meshes;
            std::vector<Ref<SampledTexture>> Textures;
            struct Batch { size_t Capacity = 0; Scope<Vulkan::Buffer> Buffer; };
            std::vector<Batch> Batches;
            size_t UsedBatches = 0;
        };
        std::array<FrameResources,2> Frames;
    };
}
