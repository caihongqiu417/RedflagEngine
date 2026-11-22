#pragma once

#include <assimp/scene.h>
#include <glm/glm.hpp>

namespace Redflag
{
    // 可绘制模型与光线追踪按相同顺序访问各部件。根节点本身也可能带有单位换算。
    template <typename Visit>
    void VisitModelMeshes(const aiScene& model, const aiNode& node, const glm::mat4& parent, Visit&& visit)
    {
        const auto& m = node.mTransformation;
        const glm::mat4 local(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2,
                              m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);
        const glm::mat4 transform = parent * local;
        for (unsigned i = 0; i < node.mNumMeshes; ++i)
            visit(*model.mMeshes[node.mMeshes[i]], transform);

        for (unsigned i = 0; i < node.mNumChildren; ++i)
            VisitModelMeshes(model, *node.mChildren[i], transform, visit);
    }
}
