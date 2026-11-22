#include "rfpch.h"
#include "GIScene.h"
#include "Redflag/Terrain/Terrain.h"
#include "GITexture.h"
#include "Redflag/Renderer/ModelNodes.h"
#include "Redflag/Renderer/ModelEmission.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/pbrmaterial.h>
#include <stb_image.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Redflag
{
    static glm::vec3 Position(const aiVector3D &p)
    {
        return {p.x, p.y, p.z};
    }

    static glm::vec3 ReadAlbedo(const aiMaterial &material, const std::filesystem::path &folder)
    {
        aiColor3D color(1, 1, 1);
        material.Get(AI_MATKEY_COLOR_DIFFUSE, color);
        glm::vec3 base{color.r, color.g, color.b};
        aiString path;
        if (material.GetTexture(aiTextureType_DIFFUSE, 0, &path) != AI_SUCCESS)
            return base;
        int width, height, channels;
        stbi_set_flip_vertically_on_load(0); // 固定从贴图顶行开始取色，避免上次加载图片时的翻转设置改变取样位置。
        auto *pixels = stbi_load((folder / path.C_Str()).string().c_str(), &width, &height, &channels, 3);
        if (!pixels)
            return base;
        // 把采到的颜色从 sRGB 转成线性 RGB，分别求红、绿、蓝的平均值，再乘材质底色。
        glm::vec3 sum(0);
        int count = 0;
        for (int y = 0; y < height; y += std::max(1, height / 64))
            for (int x = 0; x < width; x += std::max(1, width / 64))
            {
                auto p = pixels + 3 * (size_t(y) * width + x);
                glm::vec3 srgb = glm::vec3(p[0], p[1], p[2]) / 255.0f;
                for (int k = 0; k < 3; ++k)
                    sum[k] += srgb[k] <= 0.04045f ? srgb[k] / 12.92f : std::pow((srgb[k] + 0.055f) / 1.055f, 2.4f);
                ++count;
            }
        stbi_image_free(pixels);
        return base * (sum / float(count));
    }

    struct BuildBounds
    {
        glm::vec3 Min{std::numeric_limits<float>::max()}, Max{-std::numeric_limits<float>::max()};

        void Add(const BuildBounds& other)
        {
            Min = glm::min(Min, other.Min);
            Max = glm::max(Max, other.Max);
        }

        float HalfArea() const
        {
            const glm::vec3 size = Max - Min;
            return size.x * size.y + size.y * size.z + size.z * size.x;
        }
    };

    struct BuildTriangle
    {
        BuildBounds Bounds;
        glm::vec3 Center;
        uint32_t Index;
    };

    static uint32_t SplitTriangles(BuildTriangle* triangles, uint32_t count, const BuildBounds& centers)
    {
        const glm::vec3 span = centers.Max - centers.Min;
        int axis = span.x > span.y ? 0 : 1;
        if (span.z > span[axis])
            axis = 2;

        // 沿重心分布最宽的一轴分成 12 段，比较 11 个切分位置（PBRT 第四版 §7.3.2）。
        constexpr int binCount = 12;
        if (count <= binCount || span[axis] == 0.f)
        {
            // 三角形很少或重心重合时，两边各取一半即可。
            std::nth_element(triangles, triangles + count / 2, triangles + count,
                [axis](const auto& a, const auto& b) { return a.Center[axis] < b.Center[axis]; });
            return count / 2;
        }

        struct Bin { BuildBounds Bounds; uint32_t Count = 0; };
        Bin bins[binCount];
        auto binIndex = [&](const BuildTriangle& triangle) {
            const float offset = (triangle.Center[axis] - centers.Min[axis]) / span[axis];
            return std::min(int(offset * binCount), binCount - 1);
        };
        for (uint32_t i = 0; i < count; ++i)
        {
            auto& bin = bins[binIndex(triangles[i])];
            ++bin.Count;
            bin.Bounds.Add(triangles[i].Bounds);
        }

        // 盒子的表面积越大，射线越可能碰到它。两侧各用面积乘三角形数，取总和最小的位置。
        float cost[binCount - 1]{};
        BuildBounds left, right;
        uint32_t leftCount = 0, rightCount = 0;
        for (int i = 0; i < binCount - 1; ++i)
        {
            left.Add(bins[i].Bounds);
            leftCount += bins[i].Count;
            cost[i] = leftCount * left.HalfArea();
        }
        for (int i = binCount - 1; i > 0; --i)
        {
            right.Add(bins[i].Bounds);
            rightCount += bins[i].Count;
            cost[i - 1] += rightCount * right.HalfArea();
        }

        const int split = int(std::min_element(std::begin(cost), std::end(cost)) - cost);
        auto* middle = std::partition(triangles, triangles + count,
            [&](const auto& triangle) { return binIndex(triangle) <= split; });
        return uint32_t(middle - triangles);
    }

    static void BuildNodes(GIScene& scene, BuildTriangle* triangles, uint32_t first, uint32_t count)
    {
        BuildBounds bounds, centers;
        for (uint32_t i = first; i < first + count; ++i)
        {
            bounds.Add(triangles[i].Bounds);
            centers.Min = glm::min(centers.Min, triangles[i].Center);
            centers.Max = glm::max(centers.Max, triangles[i].Center);
        }

        const uint32_t node = uint32_t(scene.Nodes.size());
        scene.Nodes.push_back({{bounds.Min, 0}, {bounds.Max, 0}, {first, count, 0, 0}});
        if (count > 6)
        {
            const uint32_t left = SplitTriangles(triangles + first, count, centers);
            scene.Nodes[node].Range.y = 0;
            BuildNodes(scene, triangles, first, left);
            BuildNodes(scene, triangles, first + left, count - left);
        }
        // 子树在数组中连续排列。光线错过一个包围盒时，直接跳到子树末尾。
        scene.Nodes[node].Range.z = uint32_t(scene.Nodes.size());
    }

    static void BuildTree(GIScene& scene)
    {
        RF_PROFILE_SCOPE("GI / Build BVH");
        std::vector<BuildTriangle> triangles;
        triangles.reserve(scene.Triangles.size());
        for (const auto& t : scene.Triangles)
        {
            const glm::vec3 a(t.A), b(t.B), c(t.C);
            const BuildBounds bounds{glm::min(a, glm::min(b, c)), glm::max(a, glm::max(b, c))};
            triangles.push_back({bounds, (a + b + c) / 3.f, uint32_t(triangles.size())});
        }

        // 建树时只移动包围盒、重心和序号；顺序确定后再把完整的三角形排给 GPU。
        BuildNodes(scene, triangles.data(), 0, uint32_t(triangles.size()));
        auto source = std::move(scene.Triangles);
        scene.Triangles.reserve(source.size());
        for (const auto& triangle : triangles)
            scene.Triangles.push_back(source[triangle.Index]);
    }

    struct ImportedMask
    {
        GIMaskSettings Settings{1.f, -1.f};
        aiString OpacityPath;
        bool FromOpacityMap = false;
    };

    static ImportedMask ReadMask(const aiMaterial& material)
    {
        ImportedMask mask;
        material.GetTexture(aiTextureType_OPACITY, 0, &mask.OpacityPath);
        aiString mode;
        if (material.Get(AI_MATKEY_GLTF_ALPHAMODE, mode) == AI_SUCCESS)
        {
            if (std::strcmp(mode.C_Str(), "MASK") == 0)
            {
                mask.Settings.Cutoff = .5f; // glTF 未指定裁剪阈值时使用 0.5。
                material.Get(AI_MATKEY_GLTF_ALPHACUTOFF, mask.Settings.Cutoff);
            }
        }
        else if (mask.OpacityPath.length)
        {
            mask.Settings.Cutoff = .5f;
            mask.FromOpacityMap = true;
        }
        if (mask.Settings.Cutoff >= 0)
        {
            aiColor4D base(1,1,1,1);
            if (material.Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR, base) != AI_SUCCESS)
                material.Get(AI_MATKEY_COLOR_DIFFUSE, base);
            mask.Settings.Alpha = base.a;
        }
        return mask;
    }

    template<typename Visit>
    static void VisitParts(const aiScene& model, const GIModel& input, bool benchmark, Visit&& visit)
    {
        if (benchmark)
        {
            for (unsigned i = 0; i < model.mNumMeshes; ++i)
                visit(*model.mMeshes[i], input.Transform);
        }
        else
            VisitModelMeshes(model, *model.mRootNode, input.Transform, visit);
    }

    static void CollectMaskImages(GITextureLibrary& textures, const GIModel& input, const aiScene* model, bool benchmark)
    {
        auto collect = [&](size_t part, const aiMaterial* material) {
            if (part < input.TransparentParts.size() && input.TransparentParts[part]) return;
            const ImportedMask imported = material ? ReadMask(*material) : ImportedMask{};
            const auto edited = input.MaterialMasks.find(part);
            const auto& mask = edited == input.MaterialMasks.end() ? imported.Settings : edited->second;
            if (mask.Cutoff < 0) return;

            const auto maps = input.MaterialMaps.find(part);
            aiString baseColor;
            if (maps != input.MaterialMaps.end() && maps->second.BaseColor)
                textures.KeepMask({*maps->second.BaseColor}, GIMaskChannel::Alpha);
            else if (material && material->GetTexture(aiTextureType_DIFFUSE, 0, &baseColor) == AI_SUCCESS)
                textures.KeepMask(FindGIImage(*model, baseColor, input.Path), GIMaskChannel::Alpha);

            if (mask.Opacity)
                textures.KeepMask({*mask.Opacity}, GIMaskChannel::Red);
            else if (imported.OpacityPath.length)
                textures.KeepMask(FindGIImage(*model, imported.OpacityPath, input.Path), GIMaskChannel::Red);
        };
        if (!model) { collect(0, nullptr); return; }
        size_t part = 0;
        VisitParts(*model, input, benchmark, [&](const aiMesh& mesh, const glm::mat4&) {
            if (mesh.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) collect(part, model->mMaterials[mesh.mMaterialIndex]);
            ++part;
        });
    }

    static void AppendMesh(GIScene &scene, const GIModel &input, const aiScene* model, bool benchmark,
                           GITextureLibrary& textures)
    {
        auto readFile = [&](const std::filesystem::path& file, GIMaskChannel channel) -> glm::uvec4 {
            return textures.Read(scene, {file}, channel, true /* 多个部件可指定同一张替换图 */);
        };
        auto materialMaps = [&](size_t part, glm::uvec4& baseColor, glm::uvec4& metallicRoughness) {
            const auto found = input.MaterialMaps.find(part);
            if (found == input.MaterialMaps.end()) return;
            if (found->second.BaseColor) baseColor = readFile(*found->second.BaseColor, GIMaskChannel::Alpha);
            if (found->second.MetallicRoughness) metallicRoughness = readFile(*found->second.MetallicRoughness, GIMaskChannel::None);
        };
        int maskIndex = -1;
        auto addMask = [&](const GIMaskSettings& mask, glm::uvec4 opacity) {
            maskIndex = static_cast<int>(scene.Masks.size());
            scene.Masks.push_back({{mask.Alpha, mask.Cutoff, 0, 0}, opacity});
        };
        glm::mat4 transform = input.Transform;
        bool opaque = input.TransparentParts.empty() || !input.TransparentParts[0];
        glm::vec2 factors = input.MaterialFactors.empty() ? glm::vec2(.5f, 0.f) : input.MaterialFactors[0];
        float surfaceType = 1.f;
        auto addTriangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 color, uint32_t face = 0) {
            a = glm::vec3(transform * glm::vec4(a, 1));
            b = glm::vec3(transform * glm::vec4(b, 1));
            c = glm::vec3(transform * glm::vec4(c, 1));
            scene.Min = glm::min(scene.Min, glm::min(a, glm::min(b, c)));
            scene.Max = glm::max(scene.Max, glm::max(a, glm::max(b, c)));
            scene.Triangles.push_back({{a, opaque ? surfaceType : 0.f}, {b, factors.x}, {c, factors.y},
                                       {color * input.Tint, maskIndex >= 0 ? -float(face) : float(face)}});
        };
        if (input.TerrainChunks)
        {
            glm::uvec4 albedo{}, roughness{};
            materialMaps(0, albedo, roughness);
            for (const TerrainChunk& chunk : *input.TerrainChunks)
                for (size_t index = 0; index < chunk.Indices.size(); index += 3)
                {
                    const MeshVertex& a = chunk.Vertices[chunk.Indices[index]];
                    const MeshVertex& b = chunk.Vertices[chunk.Indices[index + 1]];
                    const MeshVertex& c = chunk.Vertices[chunk.Indices[index + 2]];
                    uint32_t face = 0;
                    if (albedo.y)
                    {
                        scene.TextureFaces.push_back({{a.TexCoord, b.TexCoord}, {c.TexCoord, 0, 0}, albedo, roughness});
                        face = uint32_t(scene.TextureFaces.size());
                    }
                    addTriangle(a.Position, b.Position, c.Position, glm::vec3(1.f), face);
                }
            return;
        }
        if (input.Path.empty())
        {
            glm::uvec4 baseColor{}, metallicRoughness{};
            materialMaps(0, baseColor, metallicRoughness);
            const auto mask = input.MaterialMasks.find(0);
            if (opaque && mask != input.MaterialMasks.end() && mask->second.Cutoff >= 0)
            {
                const auto& opacity = mask->second.Opacity;
                addMask(mask->second, opacity ? readFile(*opacity, GIMaskChannel::Red) : glm::uvec4(0));
            }
            glm::uvec4 image{};
            if (opaque && !input.MaterialEmissions.empty() && input.MaterialEmissions[0] != glm::vec3(0))
            {
                const auto settings = input.EmissionOverrides.find(0);
                float bothSides = 0;
                if (settings != input.EmissionOverrides.end())
                {
                    bothSides = settings->second.DoubleSided ? 1.f : 0.f;
                    const auto& file = settings->second.Image;
                    if (file && !file->empty()) image = textures.Read(scene, {*file});
                }
                scene.Emissions.push_back({{input.MaterialEmissions[0], bothSides}, image});
                surfaceType = float(scene.Emissions.size() + 1);
            }
            const glm::vec3 corners[] = {{-.5, -.5, -.5}, {.5, -.5, -.5}, {.5, .5, -.5}, {-.5, .5, -.5},
                                         {-.5, -.5, .5},  {.5, -.5, .5},  {.5, .5, .5},  {-.5, .5, .5}};
            const unsigned faces[][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                                         {3, 7, 6, 2}, {1, 2, 6, 5}, {0, 4, 7, 3}};
            const bool textured = baseColor.y || metallicRoughness.y || image.y || maskIndex >= 0;
            for (const auto &f : faces)
            {
                const uint32_t first = static_cast<uint32_t>(scene.TextureFaces.size()) + 1;
                if (textured)
                {
                    // 六个面的贴图朝向与 Mesh::CreateCube 相同。
                    const glm::vec3 n = glm::cross(corners[f[1]] - corners[f[0]], corners[f[2]] - corners[f[0]]);
                    auto uv = [&](unsigned corner) {
                        const glm::vec3 p = corners[corner];
                        if (n.z) return glm::vec2(p.x * n.z, p.y) + .5f;
                        if (n.x) return glm::vec2(-p.z * n.x, p.y) + .5f;
                        return glm::vec2(p.x, -p.z * n.y) + .5f;
                    };
                    const float mask = float(std::max(maskIndex, 0));
                    scene.TextureFaces.push_back({{uv(f[0]), uv(f[1])}, {uv(f[2]), 0, mask}, baseColor, metallicRoughness});
                    scene.TextureFaces.push_back({{uv(f[0]), uv(f[2])}, {uv(f[3]), 0, mask}, baseColor, metallicRoughness});
                }
                addTriangle(corners[f[0]], corners[f[1]], corners[f[2]], glm::vec3(1), textured ? first : 0);
                addTriangle(corners[f[0]], corners[f[2]], corners[f[3]], glm::vec3(1), textured ? first + 1 : 0);
            }
            return;
        }
        const auto &modelPath = input.Path;
        auto readImage = [&](const aiString& name, GIMaskChannel channel = GIMaskChannel::None) {
            return textures.Read(scene, FindGIImage(*model, name, modelPath), channel);
        };
        std::vector<glm::vec3> colors;
        struct Maps
        {
            glm::uvec4 Color{}, MetallicRoughness{}, EmissiveImage{};
            glm::vec2 Factors{.5f, 0.f};
            glm::vec3 Emission{};
            aiString EmissivePath;
            unsigned EmissiveUV = 0;
            int DoubleSided = 0;
            ImportedMask Mask;
            glm::uvec4 OpacityImage{};
        };
        std::vector<Maps> maps(model->mNumMaterials);
        // 多个部件可能共用一种材质。只有最后一个使用者也换了图，才可以省去原图的像素。
        std::vector<glm::bvec2> useOriginal(model->mNumMaterials, glm::bvec2(input.MaterialMaps.empty()));
        std::vector<bool> editedMasks(model->mNumMaterials);
        if (!input.MaterialMaps.empty() || !input.MaterialMasks.empty())
        {
            size_t part = 0;
            auto inspect = [&](const aiMesh& mesh, const glm::mat4&) {
                const auto found = input.MaterialMaps.find(part);
                glm::bvec2& original = useOriginal[mesh.mMaterialIndex];
                original.x |= found == input.MaterialMaps.end() || !found->second.BaseColor;
                original.y |= found == input.MaterialMaps.end() || !found->second.MetallicRoughness;
                const auto mask = input.MaterialMasks.find(part);
                if (mask != input.MaterialMasks.end() && mask->second.Cutoff >= 0 &&
                    (part >= input.TransparentParts.size() || !input.TransparentParts[part]))
                    editedMasks[mesh.mMaterialIndex] = true;
                ++part;
            };
            VisitParts(*model, input, benchmark, inspect);
        }
        for (unsigned i = 0; i < model->mNumMaterials; ++i)
        {
            const auto& material = *model->mMaterials[i];
            Maps& imported = maps[i];
            imported.Mask = ReadMask(material);

            if (benchmark && imported.Mask.Settings.Cutoff < 0 && !editedMasks[i])
                colors.push_back(ReadAlbedo(material, modelPath.parent_path()));
            else
            {
                aiColor3D base(1, 1, 1);
                material.Get(AI_MATKEY_COLOR_DIFFUSE, base);
                colors.emplace_back(base.r, base.g, base.b);
                aiString image;
                if (useOriginal[i].x && material.GetTexture(aiTextureType_DIFFUSE, 0, &image) == AI_SUCCESS)
                    maps[i].Color = readImage(image, GIMaskChannel::Alpha);
                if (material.GetTexture(aiTextureType_UNKNOWN, 0, &image) == AI_SUCCESS)
                {
                    if (useOriginal[i].y)
                        maps[i].MetallicRoughness = readImage(image);
                    maps[i].Factors = {1, 1};
                }
                material.Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, maps[i].Factors.x);
                material.Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, maps[i].Factors.y);
            }
            material.GetTexture(aiTextureType_EMISSIVE, 0, &maps[i].EmissivePath);
            material.Get(AI_MATKEY_GLTF_TEXTURE_TEXCOORD(aiTextureType_EMISSIVE, 0), maps[i].EmissiveUV);
            material.Get(AI_MATKEY_TWOSIDED, maps[i].DoubleSided);
            maps[i].Emission = GetModelEmission(material);
        }
        size_t part = 0;
        auto appendPart = [&](const aiMesh& mesh, const glm::mat4& world) {
            transform = world;
            glm::vec3 color = colors[mesh.mMaterialIndex];
            if (part < input.MaterialColors.size() && input.MaterialColors[part].w > 0)
                color = glm::vec3(input.MaterialColors[part]);
            auto& material = maps[mesh.mMaterialIndex];
            glm::uvec4 baseColor = material.Color, metallicRoughness = material.MetallicRoughness;
            materialMaps(part, baseColor, metallicRoughness);
            factors = part < input.MaterialFactors.size() ? input.MaterialFactors[part] : material.Factors;
            opaque = part >= input.TransparentParts.size() || !input.TransparentParts[part];
            const auto editedMask = input.MaterialMasks.find(part);
            const auto& mask = editedMask == input.MaterialMasks.end() ? material.Mask.Settings : editedMask->second;
            maskIndex = -1;
            if (opaque && mask.Cutoff >= 0 && (mesh.mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
            {
                glm::uvec4 opacity{};
                if (mask.Opacity)
                    opacity = readFile(*mask.Opacity, GIMaskChannel::Red);
                else if (material.Mask.OpacityPath.length)
                {
                    if (!material.OpacityImage.y)
                        material.OpacityImage = readImage(material.Mask.OpacityPath, GIMaskChannel::Red);
                    opacity = material.OpacityImage;
                }
                // 旧模型只靠透明贴图识别镂空材质。原图读不到时，模型加载器会把它当作不透明表面。
                if (editedMask != input.MaterialMasks.end() || !material.Mask.FromOpacityMap || opacity.y)
                    addMask(mask, opacity);
            }
            const bool useBaseUV = maskIndex >= 0 || ((baseColor.y || metallicRoughness.y) &&
                                   (mesh.HasTextureCoords(0) || input.MaterialMaps.count(part)));
            const auto& emission = part < input.MaterialEmissions.size() ? input.MaterialEmissions[part] : material.Emission;
            glm::uvec4 emissionImage{};
            const unsigned emissionUV = material.EmissiveUV;
            surfaceType = 1.f;
            if (opaque && emission != glm::vec3(0))
            {
                const auto settings = input.EmissionOverrides.find(part);
                bool bothSides = material.DoubleSided != 0;
                const std::filesystem::path* file = nullptr;
                if (settings != input.EmissionOverrides.end())
                {
                    bothSides = settings->second.DoubleSided;
                    if (settings->second.Image) file = &*settings->second.Image;
                }
                if (file)
                {
                    if (!file->empty() && mesh.HasTextureCoords(emissionUV)) emissionImage = textures.Read(scene, {*file});
                }
                else if (material.EmissivePath.length && mesh.HasTextureCoords(emissionUV))
                {
                    if (!material.EmissiveImage.y)
                        material.EmissiveImage = readImage(material.EmissivePath);
                    emissionImage = material.EmissiveImage;
                }
                scene.Emissions.push_back({{emission, float(bothSides)}, emissionImage});
                surfaceType = float(scene.Emissions.size() + 1);
            }
            ++part;
            for (unsigned f = 0; f < mesh.mNumFaces; ++f)
            {
                auto &face = mesh.mFaces[f];
                if (face.mNumIndices != 3)
                    continue;
                glm::vec3 a = Position(mesh.mVertices[face.mIndices[0]]);
                glm::vec3 b = Position(mesh.mVertices[face.mIndices[1]]);
                glm::vec3 c = Position(mesh.mVertices[face.mIndices[2]]);
                uint32_t textureFace = 0;
                const bool emitUV = emissionImage.y != 0;
                if (useBaseUV || emitUV)
                {
                    auto faceUV = [&](unsigned channel) {
                        const auto* uv = mesh.mTextureCoords[channel];
                        // 没有 UV 的模型在绘制时使用 (0, 0)，反射光线也取贴图上的同一点。
                        if (!uv) return GITextureFace{{}, {}, baseColor, metallicRoughness};
                        const auto &u = uv[face.mIndices[0]], &v = uv[face.mIndices[1]], &w = uv[face.mIndices[2]];
                        return GITextureFace{{u.x,u.y,v.x,v.y},{w.x,w.y,0,0},baseColor,metallicRoughness};
                    };
                    auto coordinates = faceUV(useBaseUV ? 0 : emissionUV);
                    if (maskIndex >= 0) coordinates.UV2.w = float(maskIndex);
                    if (useBaseUV && emitUV && emissionUV != 0)
                    {
                        scene.TextureFaces.push_back(faceUV(emissionUV));
                        coordinates.UV2.z = float(scene.TextureFaces.size());
                    }
                    scene.TextureFaces.push_back(coordinates);
                    textureFace = static_cast<uint32_t>(scene.TextureFaces.size());
                }
                addTriangle(a, b, c, color, textureFace);
            }
        };
        VisitParts(*model, input, benchmark, appendPart);
    }

    GIScene LoadGIScene(const std::vector<GIModel> &models, bool normalize)
    {
        GIScene scene;
        // 场景只有动画角色时，直接光仍可照亮角色，静态光线则找不到三角形。
        if (models.empty() && !normalize) {
            scene.Min = scene.Max = glm::vec3(0);
            return scene;
        }
        scene.Min = glm::vec3(std::numeric_limits<float>::max());
        scene.Max = -scene.Min;
        // 先看哪些材质把同一张图用作底色或透明图，解码一次即可取出需要的通道。
        GITextureLibrary textures;
        struct ModelFile
        {
            Assimp::Importer Importer;
            size_t Uses = 0;
        };
        std::map<std::filesystem::path, ModelFile> imports;
        for (const auto &input : models)
        {
            const aiScene* model = nullptr;
            if (!input.Path.empty())
            {
                auto [it, added] = imports.try_emplace(input.Path);
                Assimp::Importer& importer = it->second.Importer;
                if (added && !importer.ReadFile(input.Path.string(), aiProcess_Triangulate |
                                                (normalize ? aiProcess_PreTransformVertices : 0)))
                    throw std::runtime_error(importer.GetErrorString());
                model = importer.GetScene();
                ++it->second.Uses;
            }
            CollectMaskImages(textures, input, model, normalize);
        }
        for (const auto& input : models)
        {
            auto file = imports.find(input.Path);
            const aiScene* model = file == imports.end() ? nullptr : file->second.Importer.GetScene();
            AppendMesh(scene, input, model, normalize, textures);
            // 最后一个实例的三角形已复制出来，建树时无需再保留 Assimp 的原网格和内嵌图。
            if (file != imports.end() && --file->second.Uses == 0) imports.erase(file);
        }
        if (scene.Triangles.empty())
            throw std::runtime_error("GI scene contains no triangles");
        if (normalize)
        {
            const glm::vec3 center = (scene.Min + scene.Max) * .5f, size = scene.Max - scene.Min;
            float scale = 20.0f / std::max({size.x, size.y, size.z});
            scene.Min = (scene.Min - center) * scale;
            scene.Max = (scene.Max - center) * scale;
            for (auto &t : scene.Triangles)
            {
                t.A = glm::vec4((glm::vec3(t.A) - center) * scale, t.A.w);
                t.B = glm::vec4((glm::vec3(t.B) - center) * scale, t.B.w);
                t.C = glm::vec4((glm::vec3(t.C) - center) * scale, t.C.w);
            }
        }
        BuildTree(scene);
        return scene;
    }
}
