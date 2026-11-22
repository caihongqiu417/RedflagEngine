#include "rfpch.h"
#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "RenderDocCapture.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Renderer/GI/GlobalIllumination.h"
#include "Redflag/Renderer/IBLArchive.h"
#include "Redflag/Renderer/IBLPrecompute.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <array>
#include <fstream>
#include <iostream>

using namespace Redflag;
namespace fs = std::filesystem;

constexpr int Tile = 65, Width = Tile * 4, Height = Tile * 3;
constexpr float Roughness[] = {.1f, .4f, .7f, 1.f};
const glm::vec4 Materials[] = {{1, 1, 1, 1}, {.95f, .55f, .2f, 1}, {0, 0, 0, 0}};
const fs::path Bake = "bin-int/Tests/test-room.rfibl";

// 白金属、有色金属和黑塑料各一行。两个后端用同一组球面法线，每列的粗糙度相同。
static std::vector<GISurface> Spheres()
{
    std::vector<GISurface> surfaces(Width * Height);
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x)
        {
            glm::vec2 xy(x % Tile - Tile / 2, y % Tile - Tile / 2);
            xy /= float(Tile / 2);
            float z2 = 1.f - glm::dot(xy, xy);
            if (z2 <= 0.f)
                continue;
            glm::vec4 p(2.f * (x + .5f) / Width - 1.f, 2.f * (y + .5f) / Height - 1.f, 0, 1);
            surfaces[y * Width + x] = {p, {xy, std::sqrt(z2), Roughness[x / Tile]}, Materials[y / Tile]};
        }
    return surfaces;
}

static void Write(const fs::path& path, const std::vector<glm::vec4>& pixels)
{
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char*>(pixels.data()), pixels.size() * sizeof(pixels[0]));
}

static GLuint Program(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    std::string source((std::istreambuf_iterator<char>(input)), {});
    size_t vertex = source.find("#version"), separator = source.find("#type fragment", vertex);
    GLuint program = glCreateProgram();
    for (GLenum stage : {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER})
    {
        std::string text = stage == GL_VERTEX_SHADER ? source.substr(vertex, separator - vertex)
                                                     : source.substr(source.find("#version", separator));
        const char* code = text.c_str();
        GLuint shader = glCreateShader(stage);
        glShaderSource(shader, 1, &code, nullptr);
        glCompileShader(shader);
        GLint pass;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &pass);
        if (!pass)
        {
            char message[2048];
            glGetShaderInfoLog(shader, sizeof(message), nullptr, message);
            throw std::runtime_error(message);
        }
        glAttachShader(program, shader);
        glDeleteShader(shader);
    }
    glLinkProgram(program);
    GLint pass;
    glGetProgramiv(program, GL_LINK_STATUS, &pass);
    if (!pass)
        throw std::runtime_error("Cannot link the engine mesh shader");
    return program;
}

static void OpenGL(const fs::path& work, const std::vector<GISurface>& surfaces)
{
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        throw std::runtime_error("OpenGL unavailable");
    if (!IBLPrecompute::LoadBaked(Bake))
        throw std::runtime_error("Cannot load the baked environment");
    IBLPrecompute::Bind();
    const IBLTextures& sky = IBLPrecompute::GetTextures();
    const glm::vec4 white(1), black(0);
    for (int i = 0; i < IBLTextures::ReflectionLevels; ++i)
        glClearTexImage(sky.PrefilterMap, i, GL_RGB, GL_FLOAT, &white.x);
    glClearTexImage(sky.IrradianceMap, 0, GL_RGB, GL_FLOAT, &white.x);

    // 只有环境光。一盏强度为零的方向光让场景不启用默认灯。SSAO 全白，RSM 全黑。
    GLuint buffers[4], vao, fbo, textures[3];
    glCreateBuffers(4, buffers);
    for (int i = 0; i < 4; ++i)
    {
        std::array<float, 512> data{};
        glNamedBufferData(buffers[i], sizeof(data), data.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, i, buffers[i]);
    }
    glm::mat4 identity(1);
    glm::vec4 eye(0, 0, 1.e6f, 1), viewport(0, 0, Width, Height), light(0, 0, -1, 0);
    int one = 1;
    glNamedBufferSubData(buffers[0], 0, 64, &identity);
    glNamedBufferSubData(buffers[0], 64, 16, &eye);
    glNamedBufferSubData(buffers[0], 160, 16, &viewport);
    glNamedBufferSubData(buffers[1], 0, 64, &identity);
    glNamedBufferSubData(buffers[3], 0, 4, &one);
    glNamedBufferSubData(buffers[3], 16, 16, &light);

    glCreateTextures(GL_TEXTURE_2D, 3, textures);
    for (int i = 0; i < 3; ++i)
    {
        glTextureStorage2D(textures[i], 1, GL_RGBA32F, i == 0 ? Width : 1, i == 0 ? Height : 1);
        glClearTexImage(textures[i], 0, GL_RGBA, GL_FLOAT, i == 2 ? &black.x : &white.x);
        glTextureParameteri(textures[i], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }
    glBindTextureUnit(12, textures[1]);
    glBindTextureUnit(17, textures[2]);
    glCreateVertexArrays(1, &vao);
    glBindVertexArray(vao);
    std::vector<GISurface> points;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 4; ++col)
            for (int y = row * Tile; y < (row + 1) * Tile; ++y)
                for (int x = col * Tile; x < (col + 1) * Tile; ++x)
                    if (surfaces[y * Width + x].Position.w != 0.f)
                        points.push_back(surfaces[y * Width + x]);

    GLuint vertices;
    glCreateBuffers(1, &vertices);
    glNamedBufferData(vertices, points.size() * sizeof(GISurface), points.data(), GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(vao, 0, vertices, 0, sizeof(GISurface));
    for (int attribute = 0; attribute < 2; ++attribute)
    {
        glEnableVertexArrayAttrib(vao, attribute);
        glVertexArrayAttribFormat(vao, attribute, 3, GL_FLOAT, GL_FALSE,
                                  attribute == 0 ? 0 : offsetof(GISurface, Normal));
        glVertexArrayAttribBinding(vao, attribute, 0);
    }
    const GLsizei pointsPerSphere = GLsizei(points.size() / 12);
    glVertexAttrib3f(3, 1, 0, 0);
    glCreateFramebuffers(1, &fbo);
    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, textures[0], 0);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, Width, Height);
    std::cout << "OpenGL " << glGetString(GL_RENDERER) << '\n';

    for (const std::string pass : {"before", "after", "black"})
    {
        bool dark = pass == "black";
        if (dark)
        {
            glClearTexImage(sky.IrradianceMap, 0, GL_RGB, GL_FLOAT, &black.x);
            for (int i = 0; i < IBLTextures::ReflectionLevels; ++i)
                glClearTexImage(sky.PrefilterMap, i, GL_RGB, GL_FLOAT, &black.x);
        }
        GLuint shader = Program(work / (pass == "before" ? "before.glsl" : "after.glsl"));
        glUseProgram(shader);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 4; ++col)
            {
                glm::vec4 color(glm::vec3(Materials[row]), 1);
                glm::vec3 settings(Materials[row].a, Roughness[col], 1);
                glNamedBufferSubData(buffers[2], 0, 16, &color);
                glNamedBufferSubData(buffers[2], 16, 12, &settings);
                glDrawArrays(GL_POINTS, (row * 4 + col) * pointsPerSphere, pointsPerSphere);
            }
        std::vector<glm::vec4> pixels(surfaces.size());
        glReadPixels(0, 0, Width, Height, GL_RGBA, GL_FLOAT, pixels.data());
        Write(work / ("opengl-" + pass + ".rgba"), pixels);
        glDeleteProgram(shader);
    }
    if (glGetError() != GL_NO_ERROR)
        throw std::runtime_error("OpenGL furnace draw failed");
    glDeleteBuffers(1, &vertices);
    glDeleteBuffers(4, buffers);
    glDeleteTextures(3, textures);
    glDeleteFramebuffers(1, &fbo);
    glDeleteVertexArrays(1, &vao);
    IBLPrecompute::Shutdown();
}

static void Vulkan(const fs::path& work, const std::vector<GISurface>& surfaces, GLFWwindow* window,
                   RenderDocCapture& capture)
{
    VulkanContext context(window);
    context.Init();
    VulkanComputeDevice device(context);
    struct
    {
        glm::mat4 matrices[5];
        glm::vec4 eye;
        glm::uvec4 extent;
        glm::vec4 options[3];
    } frame{};
    frame.matrices[0] = frame.matrices[1] = glm::mat4(1);
    frame.extent = {Width, Height, 0, 0};

    std::vector<glm::vec4> lighting, pixels(surfaces.size());
    if (!IBLArchive::ReadLighting(Bake, lighting))
        throw std::runtime_error("Cannot read the BRDF coefficients");
    size_t table = lighting.size() - size_t(IBLTextures::LutSize) * IBLTextures::LutSize;
    std::fill(lighting.begin(), lighting.begin() + table, glm::vec4(1));
    for (size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = glm::vec4(surfaces[i].Position.w == 0.f ? 1.f : 0.f);

    auto camera = device.CreateBuffer(sizeof(frame), &frame);
    auto geometry = device.CreateBuffer(surfaces.size() * sizeof(GISurface), surfaces.data());
    size_t bytes = pixels.size() * sizeof(pixels[0]);
    auto color = device.CreateBuffer(bytes, pixels.data());
    auto result = device.CreateBuffer(bytes);
    auto sky = device.CreateBuffer(lighting.size() * sizeof(lighting[0]), lighting.data());

    for (const std::string pass : {"before", "after", "black"})
    {
        if (pass == "black")
        {
            std::fill(lighting.begin(), lighting.begin() + table, glm::vec4(0));
            device.Upload(sky, lighting.data(), lighting.size() * sizeof(lighting[0]));
        }
        std::ifstream binary(work / (pass == "before" ? "before.spv" : "after.spv"),
                             std::ios::binary | std::ios::ate);
        size_t length = size_t(binary.tellg());
        std::vector<uint32_t> words(length / 4);
        binary.seekg(0);
        binary.read(reinterpret_cast<char*>(words.data()), length);
        const uint32_t options[] = {1, 0, 1}; // 实际材质，关闭屏幕追踪，开启环境反射。
        auto pipeline = device.CreatePipeline(
            words.data(), length, std::vector<ComputeBinding>(5, ComputeBinding::Buffer), sizeof(options));
        if (pass == "after")
            capture.Begin();
        device.Dispatch(pipeline, {camera, geometry, color, result, sky}, options, (Width + 7) / 8,
                        (Height + 7) / 8);
        if (pass == "after")
            capture.End();
        device.Readback(result, pixels.data(), bytes);
        Write(work / ("vulkan-" + pass + ".rgba"), pixels);
    }
}

int main(int argc, char** argv)
{
    Log::Init();
    if (argc < 2 || !glfwInit())
        return 1;
    try
    {
        RenderDocCapture capture(argc > 2 ? argv[2] : nullptr, fs::path(argv[1]) / "Furnace",
                                 "Rough metal in a uniform white environment",
                                 "260 x 195, three materials and four roughness levels. Linear RGB.");
        auto surfaces = Spheres();
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        GLFWwindow* gl = glfwCreateWindow(32, 32, "OpenGL furnace", nullptr, nullptr);
        glfwMakeContextCurrent(gl);
        OpenGL(argv[1], surfaces);
        glfwDestroyWindow(gl);

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        GLFWwindow* vk = glfwCreateWindow(32, 32, "Vulkan furnace", nullptr, nullptr);
        Vulkan(argv[1], surfaces, vk, capture);
        glfwDestroyWindow(vk);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
    glfwTerminate();
}
