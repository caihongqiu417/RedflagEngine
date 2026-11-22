#pragma once

#include "RHI/ComputeDevice.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <map>

namespace Redflag
{
    class Scene;

    /* 每个视口缓存自己需要的贴花图片。绘制后，直接光和环境反射读取改过的表面材质。 */
    class DecalRenderer
    {
      public:
        explicit DecalRenderer(ComputeDevice& device) : m_Device(device)
        {
        }
        bool Update(Scene& scene);
        void Draw(const Ref<ComputeResource>& surfaces, const glm::mat4& viewProjection, glm::uvec2 viewport);

      private:
        struct Projector
        {
            uint64_t Entity;
            int Order;
            std::filesystem::path File;
            glm::mat4 Box;
            glm::vec4 Tint;
            glm::vec3 Material;
            bool operator==(const Projector& other) const;
        };

        struct Image
        {
            std::filesystem::file_time_type Modified;
            Ref<ComputeResource> Pixels;
            glm::ivec2 Size{};
        };

        ComputeDevice& m_Device;
        Ref<ComputePipeline> m_Pipeline;
        std::vector<Projector> m_Projectors;
        std::map<std::filesystem::path, Image> m_Images;
    };
} // namespace Redflag
