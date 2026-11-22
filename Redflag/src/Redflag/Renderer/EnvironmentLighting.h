#pragma once

#include "Redflag/Renderer/RHI/ComputeDevice.h"
#include <filesystem>
#include <glm/vec2.hpp>

namespace Redflag
{
    /* 一份场景环境可供多个视口使用。相机移动或面板换尺寸时，已算好的环境光可以继续用。 */
    namespace EnvironmentLighting
    {
        struct Panorama
        {
            Ref<ComputeResource> Pixels;
            glm::ivec2 Size{};
        };

        // HDR 全景在设备上积分一次；rfibl 文件直接上传已烘焙的材质光照。读取失败返回空引用。
        /// <param name="contentKey">记录环境像素的校验值。若传入已有的环境图，同时传入上次返回的值。</param>
        /// <param name="cached">文件读回的内容相同，可继续用这张图，省去重复的上传或 HDR 积分。</param>
        /// <param name="sky">画天空时保留上传的 HDR 全景。再次读取同一文件时，和 cached 一起传回。烘焙文件直接使用其镜面图。</param>
        Ref<ComputeResource> Load(ComputeDevice& device, const std::filesystem::path& path,
            uint64_t* contentKey = nullptr, const Ref<ComputeResource>& cached = nullptr, Panorama* sky = nullptr);
    }
}
