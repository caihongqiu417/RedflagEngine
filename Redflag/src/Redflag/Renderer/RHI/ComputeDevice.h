#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include "Redflag/Core/Core.h"

namespace Redflag {

    enum class ComputeBinding { Buffer, Image };

    struct ComputeResource
    {
        virtual ~ComputeResource() = default;
    };

    struct ComputePipeline
    {
        virtual ~ComputePipeline() = default;
    };

    /* 光照和屏幕效果用这台设备创建 GPU 数组或图像，并运行读写它们的计算着色器。 */
    class ComputeDevice
    {
    public:
        virtual ~ComputeDevice() = default;

        // ---- 数据与图像 ----
        virtual Ref<ComputeResource> CreateBuffer(size_t bytes, const void* initial = nullptr) = 0;

        /// <summary>每个像素用四个连续的 float 保存 RGBA。像素 (0, 0) 对应内存的前四个数，逐行排列。</summary>
        virtual Ref<ComputeResource> CreateImage(uint32_t width, uint32_t height, const void* rgba32f = nullptr) = 0;

        virtual void Upload(const Ref<ComputeResource>& resource, const void* src, size_t bytes) = 0;

        virtual void Readback(const Ref<ComputeResource>& resource, void* dst, size_t bytes) = 0;

        // ---- 着色器与调度 ----
        /// <summary>
        /// 根据编译好的计算着色器创建管线，供后续 Dispatch 使用。
        /// </summary>
        /// <param name="spirvBytes">着色器二进制的字节数。一个 SPIR-V 字占四个字节。</param>
        /// <param name="bindings">按着色器中 set 0 的 binding 编号排列。例如先读颜色数组、再写面板图，就依次传 Buffer、Image。</param>
        virtual Ref<ComputePipeline> CreatePipeline(const uint32_t* spirv, size_t spirvBytes,
            const std::vector<ComputeBinding>& bindings, uint32_t constantBytes = 0) = 0;

        /// <summary>
        /// 运行一次计算着色器。函数返回时，下一次计算或 CPU 读回就能使用结果。
        /// </summary>
        /// <param name="resources">本次读写的资源，顺序与创建管线时列出的 bindings 一致。</param>
        /// <param name="groupsX">横向的工作组数。例如每组覆盖 8 个像素，17 像素宽就需要 3 组。Y 和 Z 同理。</param>
        virtual void Dispatch(const Ref<ComputePipeline>& pipeline, const std::vector<Ref<ComputeResource>>& resources,
            const void* constants, uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) = 0;
    };
}
