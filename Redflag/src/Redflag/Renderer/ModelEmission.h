#pragma once

#include <assimp/material.h>
#include <glm/vec3.hpp>

namespace Redflag
{
    // glTF 导入器已将 emissiveStrength 乘入颜色；缺少自发光因子时保持黑色。
    inline glm::vec3 GetModelEmission(const aiMaterial& material)
    {
        aiColor3D color(0.f, 0.f, 0.f);
        material.Get(AI_MATKEY_COLOR_EMISSIVE, color);
        return {color.r, color.g, color.b};
    }
}
