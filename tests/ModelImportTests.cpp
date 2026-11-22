#include "rfpch.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Renderer/Model.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
    // 测试输入使用常量灰度 TGA，真实经过文件解码和模型材质导入。
    void WriteImage(const std::filesystem::path& path, int width, int height, uint8_t value)
    {
        std::array<uint8_t, 18> header{};
        header[2] = 2;
        header[12] = uint8_t(width);
        header[14] = uint8_t(height);
        header[16] = 24;
        std::vector<uint8_t> pixels(size_t(width * height * 3), value);
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(header.data()), header.size());
        out.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
        assert(out.good());
    }

    void CheckImport(const std::string& name, int mw, int mh, int rw, int rh, bool expectMap,
        const std::string& materialName = "Surface", bool normalMap = false)
    {
        const auto directory = std::filesystem::path("bin-int/Tests/ModelImportFixtures") / name;
        std::filesystem::create_directories(directory);
        {
            std::ofstream model(directory / "triangle.obj");
            model << "mtllib triangle.mtl\n"
                     "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                     "vt 0 0\nvt 1 0\nvt 0 1\n"
                     "vn 0 0 1\nusemtl " << materialName << "\nf 1/1/1 2/2/1 3/3/1\n";
            std::ofstream material(directory / "triangle.mtl");
            material << "newmtl " << materialName << "\nKd 1 1 1\n";
        }
        if (mw) WriteImage(directory / "surface_metal.tga", mw, mh, 64);
        if (rw) WriteImage(directory / "surface_rough.tga", rw, rh, 192);
        if (normalMap) WriteImage(directory / "surface_nor_gl.tga", 2, 2, 128);

        auto model = Redflag::Model::Load((directory / "triangle.obj").generic_string());
        assert(model && model->GetSubMeshes().size() == 1);
        assert(model->GetSubMeshes().front().MeshData);
        const auto& material = model->GetSubMeshes().front().Mat;
        assert(material && material->HasMetallicRoughnessMap() == expectMap);
        assert(material->Alpha == Redflag::AlphaMode::Opaque);
        assert(material->HasNormalMap() == normalMap);
        assert(material->BaseColor == glm::vec4(1.0f));
        if (!expectMap)
            assert(material->Metallic == 0.0f && material->Roughness == 0.5f);
        if (expectMap)
        {
            assert(material->Metallic == 1.0f && material->Roughness == 1.0f);
            const auto& texture = material->MetallicRoughnessMap;
            const int width = mw ? mw : rw, height = mw ? mh : rh;
            assert(texture->GetWidth() == width && texture->GetHeight() == height);
            std::vector<uint8_t> pixels(size_t(width * height * 4));
            glGetTextureImage(texture->GetRendererID(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                GLsizei(pixels.size()), pixels.data());
            for (size_t i = 0; i < pixels.size(); i += 4)
            {
                assert(pixels[i] == 0 && pixels[i + 3] == 255);
                assert(pixels[i + 1] == (rw ? 192 : 128));
                assert(pixels[i + 2] == (mw ? 64 : 0));
            }
        }
        assert(glGetError() == GL_NO_ERROR);
    }

    bool CheckGltfMaterials()
    {
        const std::filesystem::path directory = "bin-int/Tests/ModelImportFixtures/explicit_factors";
        std::filesystem::create_directories(directory);
        const uint8_t whitePNG[] = {
            0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0,0,0,0x0d,0x49,0x48,0x44,0x52,
            0,0,0,1,0,0,0,1,8,2,0,0,0,0x90,0x77,0x53,0xde,0,0,0,0x0c,0x49,0x44,0x41,
            0x54,0x78,0x9c,0x63,0xf8,0xff,0xff,0x3f,0,5,0xfe,2,0xfe,0x0d,0xef,0x46,
            0xb8,0,0,0,0,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82 };
        {
            // 旁边另一件道具使用的图片，场景里这两个材质都没有引用。
            WriteImage(directory / "other_nor_gl.tga", 1, 1, 128);
            WriteImage(directory / "other_metal.tga", 1, 1, 255);
            std::ofstream image(directory / "white.png", std::ios::binary);
            image.write(reinterpret_cast<const char*>(whitePNG), sizeof(whitePNG));
            const float positions[] = { 0,0,0, 1,0,0, 0,1,0 };
            std::ofstream geometry(directory / "triangle.bin", std::ios::binary);
            geometry.write(reinterpret_cast<const char*>(positions), sizeof(positions));
            std::ofstream scene(directory / "triangle.gltf");
            scene << R"({
                "asset":{"version":"2.0"},
                "buffers":[{"uri":"triangle.bin","byteLength":36}],
                "bufferViews":[{"buffer":0,"byteLength":36}],
                "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],
                "images":[{"uri":"white.png"}],"textures":[{"source":0}],
                "extensionsUsed":["KHR_materials_emissive_strength"],
                "materials":[{"emissiveFactor":[0.125,0.5,0.875],"emissiveTexture":{"index":0},
                    "doubleSided":true,"extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":4}},
                    "pbrMetallicRoughness":{
                    "baseColorFactor":[0,0,0,0.25],"baseColorTexture":{"index":0},
                    "metallicFactor":0,"roughnessFactor":0,"metallicRoughnessTexture":{"index":0}}},
                    {"extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":8}},
                     "pbrMetallicRoughness":{"baseColorFactor":[0.2,0.3,0.4,1]}}],
                "meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0},
                                         {"attributes":{"POSITION":0},"material":1}]}],
                "nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0
            })";
        }
        auto model = Redflag::Model::Load((directory / "triangle.gltf").generic_string());
        assert(model && model->GetSubMeshes().size() == 2);
        // 文件只有位置。观察真正上传给显卡的三个顶点，平面朝向 +Z。
        for (const auto& part : model->GetSubMeshes())
        {
            part.MeshData->GetVertexArray()->GetVertexBuffers().front()->Bind();
            std::array<Redflag::MeshVertex, 3> vertices;
            glGetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
            for (const auto& vertex : vertices)
                assert(glm::distance(vertex.Normal, glm::vec3(0, 0, 1)) < .00001f);
        }
        const auto& material = model->GetSubMeshes().front().Mat;
        assert(material->BaseColorMap && material->MetallicRoughnessMap);
        assert(material->Emission == glm::vec3(.5f,2,3.5f) && material->EmissiveMap && material->DoubleSided);
        assert(material->BaseColor == glm::vec4(0, 0, 0, 0.25f));
        assert(material->Metallic == 0.0f && material->Roughness == 0.0f);
        const auto& plain = model->GetSubMeshes()[1].Mat;
        assert(plain->Emission == glm::vec3(0) && !plain->EmissiveMap && !plain->DoubleSided);
        if (material->NormalMap || plain->NormalMap || plain->MetallicRoughnessMap)
        {
            std::cerr << "glTF material picked up an unrelated texture from its folder\n";
            return false;
        }
        assert(plain->Metallic == 1.0f && plain->Roughness == 1.0f);
        // 同样的材质再从 GLB 读取。这次顶点在二进制块里，PNG 仍在模型旁。
        std::ifstream source(directory / "triangle.gltf", std::ios::binary);
        std::string json((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
        const std::string uri = "\"uri\":\"triangle.bin\",";
        json.erase(json.find(uri), uri.size());
        json.resize((json.size()+3) & ~size_t(3), ' ');
        const uint32_t binHeader[] = {36, 0x004e4942};
        const uint32_t header[] = {0x46546c67, 2, uint32_t(json.size()+20+sizeof(binHeader)+36), uint32_t(json.size()), 0x4e4f534a};
        {
            std::ofstream glb(directory / "triangle.glb",std::ios::binary);
            glb.write(reinterpret_cast<const char*>(header),sizeof(header));
            glb.write(json.data(),json.size());
            glb.write(reinterpret_cast<const char*>(binHeader),sizeof(binHeader));
            glb << std::ifstream(directory / "triangle.bin",std::ios::binary).rdbuf();
        }
        auto binary = Redflag::Model::Load((directory / "triangle.glb").generic_string());
        assert(binary && binary->GetSubMeshes().size() == 2);
        const auto& light = binary->GetSubMeshes().front().Mat;
        assert(light->Emission == material->Emission && light->DoubleSided && light->EmissiveMap);
        std::cout << "glTF and GLB emission: (0.125, 0.5, 0.875) x 4 = (0.5, 2, 3.5); missing factor stays black\n";
        assert(glGetError() == GL_NO_ERROR);
        return true;
    }
}

int main()
{
    Redflag::Log::Init();
    assert(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(32, 32, "ModelImportTests", nullptr, nullptr);
    assert(window);
    glfwMakeContextCurrent(window);
    assert(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));

    CheckImport("matched", 2, 2, 2, 2, true);
    CheckImport("rough_smaller", 2, 2, 1, 1, false);
    CheckImport("metal_smaller", 1, 1, 2, 2, false);
    CheckImport("different_shape", 2, 2, 1, 4, false);
    CheckImport("metal_only", 2, 2, 0, 0, true);
    CheckImport("rough_only", 0, 0, 2, 2, true);
    CheckImport("no_maps", 0, 0, 0, 0, false);
    // 名称和文件路径不能改变几何、金属度或法线贴图。
    CheckImport("glass_named", 0, 0, 0, 0, false, "glass", true);
    CheckImport("mirror_path", 0, 0, 0, 0, false, "mirror", true);
    if (!CheckGltfMaterials()) return 1;

    // 所有 Model/Texture Ref 已离开作用域，再销毁图形上下文。
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "Model import tests passed (9 OBJ cases and a two-material glTF)\n";
}
