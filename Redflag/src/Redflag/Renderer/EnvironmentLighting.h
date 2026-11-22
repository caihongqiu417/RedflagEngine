#pragma once

#include "Redflag/Renderer/RHI/ComputeDevice.h"
#include <filesystem>

namespace Redflag
{
    /* 一份场景环境可供多个视口使用。相机移动或面板换尺寸时，已算好的环境光可以继续用。 */
    namespace EnvironmentLighting
    {
        // HDR 全景在设备上积分一次；rfibl 文件直接上传已烘焙的材质光照。读取失败返回空引用。
        Ref<ComputeResource> Load(ComputeDevice& device, const std::filesystem::path& path);
    }
}
