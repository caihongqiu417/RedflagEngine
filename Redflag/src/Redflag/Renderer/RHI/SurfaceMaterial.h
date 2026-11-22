#pragma once

#include <memory>
#include "Redflag/Core/Core.h"
#include <glm/glm.hpp>

namespace Redflag {
    struct SampledTexture;

    /* 一次网格绘制使用的表面参数。贴图来自材质，命令列表只在此次绘制中选择它们。 */
    struct SurfaceMaterial
    {
        Ref<SampledTexture> Normal, Opacity;
        Ref<SampledTexture> MetallicRoughnessMap; // glTF 用绿色存粗糙度、蓝色存金属度。
        float Metallic = 0.0f, Roughness = 0.5f;
        bool Blend = false;
        bool DoubleSided = false;
        float RefractionIndex = 1.0f;
        float Thickness = 0.0f; // 沿玻璃法线量的厚度，单位与场景坐标相同。填 0 时直接读原处的背景。
        float AlphaCutoff = -1.0f; // 叶片透明度低于此值时，那个像素露出背景。负值表示不裁剪。
        glm::vec3 Emission{0}; // 表面自身发出的线性 RGB，亮灯时可以大于 1。
        Ref<SampledTexture> EmissiveMap;
    };
}
