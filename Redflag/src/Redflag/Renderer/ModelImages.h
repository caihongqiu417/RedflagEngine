#pragma once

#include <assimp/scene.h>
#include <charconv>

namespace Redflag
{
    // Assimp 用 *0、*1 表示模型内部的图片，具名图片则按文件名查找。
    inline const aiTexture* FindModelImage(const aiScene& scene, const aiString& name)
    {
        if (name.data[0] != '*') return scene.GetEmbeddedTexture(name.C_Str());
        unsigned index = 0;
        const char* end = name.data + name.length;
        auto result = std::from_chars(name.data + 1, end, index);
        return result.ec == std::errc{} && result.ptr == end && index < scene.mNumTextures
            ? scene.mTextures[index] : nullptr;
    }
}
