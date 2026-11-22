#include "rfpch.h"
#include "DecalRenderer.h"
#include "Redflag/Scene/Scene.h"
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <set>
#include <tuple>

namespace Redflag
{
    namespace fs = std::filesystem;

    bool DecalRenderer::Projector::operator==(const Projector& other) const
    {
        return Entity == other.Entity && Order == other.Order && File == other.File && Box == other.Box &&
               Tint == other.Tint && Material == other.Material;
    }

    // ---- 场景里的投影盒与共享贴图 ----

    bool DecalRenderer::Update(Scene& scene)
    {
        std::vector<Projector> next;
        std::set<fs::path> files;
        bool changed = false;
        auto decals = scene.GetRegistry().view<IDComponent, TransformComponent, DecalComponent>();
        for (entt::entity entity : decals)
        {
            const auto& decal = decals.get<DecalComponent>(entity);
            const auto& transform = decals.get<TransformComponent>(entity);
            if (decal.Texture.empty() || decal.Tint.a <= 0.f || glm::any(glm::lessThanEqual(decal.Size, glm::vec3(0))) ||
                glm::any(glm::equal(transform.Scale, glm::vec3(0))))
                continue;
            const fs::path file = fs::u8path(decal.Texture).lexically_normal();
            next.push_back({decals.get<IDComponent>(entity).ID,
                            decal.Order,
                            file,
                            glm::scale(transform.GetTransform(), decal.Size),
                            decal.Tint,
                            {decal.Roughness, decal.Metallic, decal.MaxAngle}});
            files.insert(file);
        }
        std::sort(next.begin(), next.end(), [](const Projector& a, const Projector& b) {
            return std::tie(a.Order, a.Entity) < std::tie(b.Order, b.Entity);
        });
        if (next != m_Projectors)
        {
            m_Projectors = std::move(next);
            changed = true;
        }

        for (auto image = m_Images.begin(); image != m_Images.end();)
            if (!files.count(image->first))
                image = m_Images.erase(image);
            else
                ++image;
        for (const fs::path& file : files)
        {
            std::error_code error;
            const fs::file_time_type modified = fs::last_write_time(file, error);
            auto found = m_Images.find(file);
            if (found != m_Images.end() && found->second.Modified == modified) continue;
            Image& image = m_Images[file];
            image = {modified};
            changed = true;
            if (error) continue;
            int channels;
            stbi_set_flip_vertically_on_load(0);
            std::ifstream input(file, std::ios::binary);
            std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
            if (bytes.empty() || bytes.size() > INT_MAX) continue;
            std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
                stbi_load_from_memory(bytes.data(), int(bytes.size()), &image.Size.x, &image.Size.y, &channels, 4),
                stbi_image_free);
            if (pixels)
                image.Pixels = m_Device.CreateBuffer(size_t(image.Size.x) * image.Size.y * 4, pixels.get());
            else
                RF_CORE_WARN("Cannot read decal texture: {0}", file.u8string());
        }
        return changed;
    }

    // ---- 把可见表面的位置换到贴花的局部坐标 ----

    static glm::ivec4 ScreenRect(const glm::mat4& clip, glm::uvec2 viewport)
    {
        glm::vec2 low(1), high(-1);
        for (int z : {-1, 1})
            for (int y : {-1, 1})
                for (int x : {-1, 1})
                {
                    const glm::vec4 p = clip * glm::vec4(x * .5f, y * .5f, z * .5f, 1);
                    // 盒子跨过镜头时，屏幕角点不能由八个除过 w 的点取得。交给像素里的盒内检测。
                    if (p.w <= 0.f) return {0, 0, viewport.x, viewport.y};
                    glm::vec2 ndc = {p.x / p.w, -p.y / p.w};
                    low = glm::min(low, ndc);
                    high = glm::max(high, ndc);
                }
        const glm::vec2 size(viewport);
        glm::ivec2 begin = glm::floor((glm::clamp(low, -1.f, 1.f) * .5f + .5f) * size);
        glm::ivec2 end = glm::ceil((glm::clamp(high, -1.f, 1.f) * .5f + .5f) * size);
        return {begin, end - begin};
    }

    void DecalRenderer::Draw(const Ref<ComputeResource>& surfaces, const glm::mat4& vp, glm::uvec2 viewport)
    {
        for (const Projector& decal : m_Projectors)
        {
            const Image& image = m_Images.at(decal.File);
            if (!image.Pixels) continue;
            const glm::ivec4 rect = ScreenRect(vp * decal.Box, viewport);
            if (rect.z <= 0 || rect.w <= 0) continue;
            if (!m_Pipeline)
            {
                static const uint32_t code[] =
#include "Platform/Vulkan/Shaders/Decal.comp.inc"
                    ;
                m_Pipeline =
                    m_Device.CreatePipeline(code, sizeof(code), {ComputeBinding::Buffer, ComputeBinding::Buffer}, 128);
            }
            struct Params
            {
                glm::mat4 Inverse;
                glm::vec4 Tint, Axis, Material;
                glm::uvec4 Screen;
            };
            static_assert(sizeof(Params) == 128);
            // 局部 +Y 指向能接收图案的表面法线。水平放置时只在朝上的表面留下印记。
            Params params{glm::inverse(decal.Box), decal.Tint,
                          glm::vec4(glm::normalize(glm::vec3(decal.Box[1])),
                                    glm::cos(glm::radians(glm::clamp(decal.Material.z, .01f, 90.f)))),
                          glm::vec4(glm::clamp(glm::vec2(decal.Material), 0.f, 1.f), image.Size.x, image.Size.y),
                          glm::uvec4(viewport, rect.x, rect.y)};
            m_Device.Dispatch(m_Pipeline, {surfaces, image.Pixels}, &params, (rect.z + 7) / 8, (rect.w + 7) / 8);
        }
    }
} // namespace Redflag
