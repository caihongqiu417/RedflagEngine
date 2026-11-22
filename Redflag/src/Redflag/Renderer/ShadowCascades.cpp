#include "rfpch.h"
#include "ShadowCascades.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Redflag
{
    std::vector<ShadowCascade> BuildShadowCascades(const glm::mat4& projection, const glm::mat4& view,
        glm::vec3 lightDirection, glm::vec3 casterMin, glm::vec3 casterMax,
        unsigned count, float maxDistance, unsigned resolution)
    {
        // 相机四条棱的两端。Redflag 在两种后端都将相机投影的近平面写为 -1，远平面写为 +1。
        const glm::mat4 inverseProjection = glm::inverse(projection);
        const glm::mat4 inverseView = glm::inverse(view);
        glm::vec3 nearCorners[4], farCorners[4];
        for (int i = 0; i < 4; ++i)
        {
            glm::vec2 corner(i & 1 ? 1.f : -1.f, i & 2 ? 1.f : -1.f);
            glm::vec4 a = inverseProjection * glm::vec4(corner, -1.f, 1.f);
            glm::vec4 b = inverseProjection * glm::vec4(corner, 1.f, 1.f);
            nearCorners[i] = glm::vec3(a) / a.w;
            farCorners[i]  = glm::vec3(b) / b.w;
        }
        const float nearDepth = -nearCorners[0].z, farDepth = -farCorners[0].z;
        const float limit = std::min(maxDistance, farDepth);
        if (limit <= nearDepth) return {};

        const glm::vec3 forward = glm::normalize(lightDirection);
        const glm::vec3 up = std::abs(forward.y) < .99f ? glm::vec3(0,1,0) : glm::vec3(0,0,1);
        const glm::mat4 lightView = glm::lookAt(glm::vec3(0), forward, up);
        const glm::vec3 casterCenter = (casterMin + casterMax) * .5f;
        const glm::vec3 casterHalf = (casterMax - casterMin) * .5f;
        const float depthCenter = glm::dot(casterCenter, forward);
        const float depthHalf = glm::dot(casterHalf, glm::abs(forward));

        std::vector<ShadowCascade> layers;
        layers.reserve(count);
        float begin = nearDepth;
        for (unsigned i = 1; i <= count; ++i)
        {
            const float t = float(i) / count;
            const float linear = glm::mix(nearDepth, limit, t);
            // 透视时近处需要更多纹素。取等距与等比切分的中点；正交画面中各处比例相同，直接等距切分。
            const float end = projection[3][3] == 0.f
                ? .5f * (linear + nearDepth * std::pow(limit / nearDepth, t)) : linear;
            glm::vec3 corners[8], center(0);
            for (int j = 0; j < 8; ++j)
            {
                float fraction = ((j < 4 ? begin : end) - nearDepth) / (farDepth - nearDepth);
                corners[j] = glm::mix(nearCorners[j % 4], farCorners[j % 4], fraction);
                center += corners[j] / 8.f;
            }

            float radius = 0;
            for (const glm::vec3 corner : corners) radius = std::max(radius, glm::length(corner - center));
            // 在相机空间量半径，转动相机时正方形大小不变。四周各留一格供过滤，再把中心对齐到整格。
            const float half = radius * resolution / (resolution - 2.f);
            const float texel = 2.f * half / resolution;
            const glm::vec3 lightCenter = lightView * inverseView * glm::vec4(center, 1.f);
            const glm::vec2 snapped = glm::round(glm::vec2(lightCenter) / texel) * texel;
            const float first = std::min(depthCenter - depthHalf, -lightCenter.z - radius) - texel;
            const float last = std::max(depthCenter + depthHalf, -lightCenter.z + radius) + texel;
            glm::mat4 lightProjection = glm::ortho(snapped.x - half, snapped.x + half,
                snapped.y - half, snapped.y + half, first, last);
            layers.push_back({lightProjection * lightView, end, texel, last - first});
            begin = end;
        }
        return layers;
    }
}
