#include "rfpch.h"
#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Redflag/Renderer/Mesh.h"
#include "RenderDocCapture.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

using namespace Redflag;
namespace fs = std::filesystem;

// 从准备好的 Sponza 三角形读取数据。相机每帧沿世界 Z 轴移动，始终朝向同一方向。
static void Run(VulkanContext& context, const fs::path& dir, RenderDocCapture& capture)
{
    std::ifstream input(dir / "wire.bin", std::ios::binary);
    input.exceptions(std::ios::badbit | std::ios::failbit);
    auto read = [&](auto& value) { input.read(reinterpret_cast<char*>(&value), sizeof(value)); };
    uint32_t vertexCount, indexCount;
    glm::ivec2 size, origin, crop;
    glm::vec3 eye, forward, lens;
    read(vertexCount); read(indexCount);
    read(size); read(origin); read(crop);
    read(eye); read(forward); read(lens);
    std::vector<MeshVertex> vertices(vertexCount);
    for (MeshVertex& vertex : vertices) {
        read(vertex.Position);
        vertex.Normal = {0,0,1};
    }
    std::vector<uint32_t> indices(indexCount);
    input.read(reinterpret_cast<char*>(indices.data()), indices.size() * sizeof(uint32_t));

    VulkanDraw draw(context, VulkanDraw::Output::LightingSurfaces);
    VulkanComputeDevice device(context);
    Ref<MeshGeometry> mesh = draw.CreateMesh(vertices, indices);
    const glm::mat4 projection = glm::perspective(lens.x, float(size.x)/size.y, lens.y, lens.z);
    // 只读回两排绳所在的矩形。按原画幅投影之后，把这个矩形放满测试图像。
    glm::mat4 cropMatrix(1);
    cropMatrix[0][0] = float(size.x)/crop.x;
    cropMatrix[1][1] = float(size.y)/crop.y;
    cropMatrix[3][0] = float(size.x - 2*origin.x)/crop.x - 1;
    cropMatrix[3][1] = 1 - float(size.y - 2*origin.y)/crop.y;
    for (int scale : {1,2,4})
    {
        const glm::ivec2 target = crop * scale;
        draw.Resize(target.x, target.y);
        std::vector<glm::vec4> surface(target.x * target.y);
        std::vector<uint8_t> mask(surface.size());
        std::ofstream output(dir / ("coverage-" + std::to_string(scale) + ".bin"), std::ios::binary);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        for (int step = 0; step <= 16; ++step)
        {
            const glm::vec3 position = eye + glm::vec3(0,0,.0055f * step);
            const glm::mat4 view = glm::lookAt(position, position + forward, glm::vec3(0,1,0));
            bool record = scale == 2 && step == 8;
            if (record) capture.Begin();
            draw.DrawSurfaces([&](CommandList& commands) {
                commands.DrawIndexed(mesh, {}, cropMatrix * projection * view, glm::mat4(1), glm::vec4(1), 1);
            });
            device.Readback(draw.GetAlbedo(), surface.data(), surface.size() * sizeof(glm::vec4));
            if (record) capture.End();
            for (size_t i = 0; i < mask.size(); ++i) mask[i] = surface[i].r > .5f;
            output.write(reinterpret_cast<const char*>(mask.data()), mask.size());
        }
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) return 1;
    fs::path dir(argv[1]);
    RenderDocCapture capture(argc > 2 ? argv[2] : nullptr, dir / "Wire-Raster", "Moving Sponza wire, 2x per axis",
        "Sponza primitive 52 with constant white albedo. Frame 8 of 16 camera steps; shaded at twice the output width and height.");
    Log::Init();
    if (!glfwInit()) return 1;
    int status = 0;
    try {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        std::unique_ptr<GLFWwindow,decltype(&glfwDestroyWindow)> window(
            glfwCreateWindow(128,128,"Wire raster",nullptr,nullptr), glfwDestroyWindow);
        VulkanContext context(window.get());
        context.Init();
        Run(context, dir, capture);
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; status = 1; }
    glfwTerminate();
    return status;
}
