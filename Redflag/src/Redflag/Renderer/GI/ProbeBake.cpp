#include "rfpch.h"
#include "ProbeBake.h"
#include "Redflag/Utils/FileSystem.h"
#include <fstream>

namespace Redflag::ProbeBake
{
    struct Header
    {
        char Magic[8];
        uint64_t SceneKey, DataHash;
    };

    uint64_t Hash(uint64_t value, const void* data, size_t bytes)
    {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < bytes; ++i)
            value = (value ^ p[i]) * 1099511628211ull;
        return value;
    }

    bool Load(const std::filesystem::path& file, uint64_t sceneKey, std::vector<glm::vec4>& values)
    {
        const size_t bytes = values.size() * sizeof(glm::vec4);
        std::ifstream in(file, std::ios::binary);
        Header header{};
        if (!in.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
            std::memcmp(header.Magic, "RFDDGI1", 8) || header.SceneKey != sceneKey)
            return false;
        return in.read(reinterpret_cast<char*>(values.data()), bytes) && in.peek() == EOF &&
               header.DataHash == Hash(HashStart, values.data(), bytes);
    }

    void Save(const std::filesystem::path& file, uint64_t sceneKey, const std::vector<glm::vec4>& values)
    {
        const size_t bytes = values.size() * sizeof(glm::vec4);
        Header header{"RFDDGI1", sceneKey, Hash(HashStart, values.data(), bytes)};
        FileSystem::Write(file, std::ios::binary, [&](std::ostream& out) {
            out.write(reinterpret_cast<const char*>(&header), sizeof(header));
            out.write(reinterpret_cast<const char*>(values.data()), bytes);
        });
    }
}
