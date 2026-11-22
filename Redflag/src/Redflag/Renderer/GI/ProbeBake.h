#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>
#include <glm/vec4.hpp>

namespace Redflag::ProbeBake
{
    constexpr uint64_t HashStart = 14695981039346656037ull;
    uint64_t Hash(uint64_t value, const void* data, size_t bytes);

    /// <summary>读回烘焙时算好的反射光。场景已改动或文件损坏时返回 false，调用者继续实时计算。</summary>
    /// <param name="values">按探针数 × 6 个方向 × 2 个 vec4 准备数组。每个方向保存收到的光，以及光线到第一个表面的距离均值和距离平方的均值。</param>
    bool Load(const std::filesystem::path& file, uint64_t sceneKey, std::vector<glm::vec4>& values);
    void Save(const std::filesystem::path& file, uint64_t sceneKey, const std::vector<glm::vec4>& values);
}
