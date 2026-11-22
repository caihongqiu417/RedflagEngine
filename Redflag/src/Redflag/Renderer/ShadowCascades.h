#pragma once

#include <vector>
#include <glm/glm.hpp>

namespace Redflag
{
    struct ShadowCascade
    {
        glm::mat4 ViewProjection;
        float FarDepth;     // 从相机往前量到这层的远端，单位与场景相同。
        float TexelSize;    // 这一层的一个阴影像素在世界中有多宽。
        float DepthRange;   // 深度图从 0 到 1 对应的世界距离，用来估算投影物离接收面有多远。
    };

    /// <summary>
    /// 将相机看见的距离分成几段，各自从方向光的角度覆盖。近处一格更小，能留下更细的影子。
    /// </summary>
    /// <param name="lightDirection">光传播的世界方向。例如正午的阳光向下照，传入 (0,-1,0)。</param>
    /// <param name="casterMin">可投影几何的世界包围盒最小角，和最大角一起确定沿光线需要画多深。</param>
    /// <param name="resolution">每张正方形阴影图的边长，以像素计。至少需要三个像素才能在两侧留边。</param>
    std::vector<ShadowCascade> BuildShadowCascades(const glm::mat4& projection, const glm::mat4& view,
        glm::vec3 lightDirection, glm::vec3 casterMin, glm::vec3 casterMax,
        unsigned count, float maxDistance, unsigned resolution);
}
