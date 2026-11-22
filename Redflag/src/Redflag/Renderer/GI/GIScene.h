#pragma once

#include <filesystem>
#include <vector>
#include <glm/glm.hpp>
#include "GlobalIllumination.h"

namespace Redflag
{
    struct GITriangle
    {
        // 直接保存三个顶点的世界坐标，相邻三角形读取同一条边时会得到相同的端点。B.w、C.w 存粗糙度和金属度。
        glm::vec4 A, B, C, Albedo; // Albedo.w 的绝对值从 1 开始指向贴图坐标；负号表示先检查透明孔。
    };
    static_assert(sizeof(GITriangle) == 4 * sizeof(glm::vec4));
    struct GINode
    {
        glm::vec4 Min, Max;
        glm::uvec4 Range; // x 是第一个三角形的下标，y 是数量（0 表示还有子节点），z 是跳过整棵子树后要查的节点。
    };

    struct GITextureFace
    {
        glm::vec4 UV01, UV2; // UV2.z 指向另用一套坐标的发光图，UV2.w 是镂空材质在 Masks 中的下标。
        glm::uvec4 Image, MetallicRoughness; // xyz 为颜色图的像素起点、宽、高。Image.w 可另指原尺寸的透明度。
    };

    struct GIEmission
    {
        glm::vec4 Color; // w 为 1 时，灯牌的两个面都发光。
        glm::uvec4 Image;
    };

    struct GIMask
    {
        glm::vec4 Parameters; // x 是绘制用的透明度，y 是裁剪阈值。
        glm::uvec4 Opacity; // 透明贴图取红通道，宽为零时按 1 计算。w 可另指原尺寸的红通道。
    };
    static_assert(sizeof(GIMask) == 32);

    struct GIScene
    {
        std::vector<GITriangle> Triangles;
        std::vector<GINode> Nodes;
        std::vector<GITextureFace> TextureFaces;
        // 图片的 w 非零时，Pixels[w-1]、Pixels[w] 是原宽和原高，之后每个 uint 从低位起放四个透明度值。
        std::vector<uint32_t> Pixels;
        std::vector<GIEmission> Emissions;
        glm::vec3 Min, Max;
        std::vector<GIMask> Masks;
        std::filesystem::path UnreadableTexture;
    };

    GIScene LoadGIScene(const std::vector<GIModel> &models, bool normalize);
}
