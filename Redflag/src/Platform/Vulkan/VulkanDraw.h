#pragma once

#include "Redflag/Renderer/RHI/RenderDevice.h"
#include "Redflag/Renderer/RHI/CommandList.h"
#include <vulkan/vulkan.h>
#include <functional>

namespace Redflag {

    class VulkanContext;

    struct ComputeResource;

    /* 网格和贴图由调用者持有。每个场景面板保留自己的颜色、深度与实体编号图。 */
    class VulkanDraw final : public RenderDevice, public CommandList
    {
    public:
        enum class Output { ColorAndEntity, LightingSurfaces, Transparency, Emission };
        explicit VulkanDraw(VulkanContext& context, Output output = Output::ColorAndEntity);
        ~VulkanDraw() override;

        // ---- 模型和底色贴图 ----
        Ref<MeshGeometry> CreateMesh(const std::vector<MeshVertex>& vertices,
            const std::vector<uint32_t>& indices) override;
        Ref<SampledTexture> CreateTexture(uint32_t width, uint32_t height, const void* rgba8) override;

        // ---- 面板绘制 ----
        bool Resize(uint32_t width, uint32_t height);
        // SSAA 把每 2×2 个光照样点合成一个像素。远处的黑白镂空贴图可据此只遮住其中一部分样点。
        void SetSubpixelMask(bool enabled);
        bool UseSceneDepth(const VulkanDraw& opaque);
        VkExtent2D GetExtent() const;
        VkImageView GetColorView() const;
        VkSampler GetSampler() const;
        void BeginScene();
        void DrawIndexed(const Ref<MeshGeometry>& mesh, const Ref<SampledTexture>& texture,
            const glm::mat4& viewProjection, const glm::mat4& transform,
            const glm::vec4& color, int entityID, const SurfaceMaterial& surface = {}) override;
        void EndScene();

        // 光照按本次相机的采样位置读取底色、表面方向和深度。
        void DrawSurfaces(const std::function<void(CommandList&)>& drawScene);
        const Ref<ComputeResource>& GetAlbedo() const;
        const Ref<ComputeResource>& GetNormalDepth() const;
        bool HasEmission() const;
        const Ref<ComputeResource>& GetEmission() const;

        bool HasTransparency() const;
        const Ref<ComputeResource>& GetOptics() const;

        struct Pixel { glm::u8vec4 Color; int EntityID; };

        /// <summary>读取上一次已提交的场景画面。先等显卡把该像素复制回来，再返回结果。</summary>
        /// <param name="x">从面板图像的左边起算的像素列，最左列为 0。</param>
        /// <param name="y">从面板图像的上边起算的像素行，最上行为 0。</param>
        /// <returns>这个像素的颜色和实体编号。背景的实体编号为 -1。</returns>
        Pixel ReadPixel(uint32_t x, uint32_t y);
    private:
        bool ResizeTarget(uint32_t width, uint32_t height, const VulkanDraw* opaque);
        void BeginScene(VkCommandBuffer cmd);
        struct Data;
        Scope<Data> m_Data;
    };
}
