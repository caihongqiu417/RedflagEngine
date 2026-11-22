#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>

namespace Redflag
{
    /* 贴花沿实体的局部 −Y 轴投向表面。XZ 是图片的宽和高，Y 是它能覆盖的厚度，单位为米。 */
    struct DecalComponent
    {
        std::string Texture;
        glm::vec3 Size{1.f, .2f, 1.f};
        glm::vec4 Tint{1.f};
        float Roughness = .7f, Metallic = 0.f;
        float MaxAngle = 75.f; // 度。调大后，倾斜的表面也能印到图案。
        int Order = 0;         // 重叠时数值大的后画。
    };
} // namespace Redflag
