#include "rfpch.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Renderer/IBLPrecompute.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace Redflag;

static std::vector<GLuint> s_VertexArrays, s_Buffers;
static PFNGLGENVERTEXARRAYSPROC s_GenVertexArrays;
static PFNGLGENBUFFERSPROC s_GenBuffers;
static int s_Failures = 0;

static void APIENTRY TrackVertexArrays(GLsizei count, GLuint* ids)
{
    s_GenVertexArrays(count, ids);
    s_VertexArrays.insert(s_VertexArrays.end(), ids, ids + count);
}

static void APIENTRY TrackBuffers(GLsizei count, GLuint* ids)
{
    s_GenBuffers(count, ids);
    s_Buffers.insert(s_Buffers.end(), ids, ids + count);
}

static void Check(bool condition, const char* message)
{
    std::cout << (condition ? "PASS " : "FAIL ") << message << '\n';
    s_Failures += !condition;
}

static void CheckReleased()
{
    bool released = true;
    for (GLuint id : s_VertexArrays) released &= !glIsVertexArray(id);
    for (GLuint id : s_Buffers) released &= !glIsBuffer(id);
    Check(released, "IBL helper vertex arrays and buffers released");
    s_VertexArrays.clear();
    s_Buffers.clear();
}

static void CheckCube(GLuint texture, int mip, const std::array<float, 3>& expected, float tolerance)
{
    GLint size;
    glGetTextureLevelParameteriv(texture, mip, GL_TEXTURE_WIDTH, &size);
    std::vector<float> pixels(size_t(size) * size * 6 * 3);
    glGetTextureImage(texture, mip, GL_RGB, GL_FLOAT, GLsizei(pixels.size() * sizeof(float)), pixels.data());
    bool valid = true;
    for (size_t i = 0; i < pixels.size(); ++i)
        valid &= std::isfinite(pixels[i]) && std::abs(pixels[i] - expected[i % 3]) < tolerance;
    Check(valid, "all six cube faces preserve constant environment color");
}

static void CheckSkyboxAttachments()
{
    GLuint framebuffer, attachments[4];
    glCreateFramebuffers(1, &framebuffer);
    glCreateTextures(GL_TEXTURE_2D, 4, attachments);
    const GLenum formats[] = {GL_RGBA16F, GL_R32I, GL_RGBA16F, GL_DEPTH_COMPONENT32F};
    for (int i = 0; i < 4; ++i)
    {
        glTextureStorage2D(attachments[i], 1, formats[i], 4, 4);
        glNamedFramebufferTexture(framebuffer, i == 3 ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0 + i,
                                  attachments[i], 0);
    }
    const GLenum outputs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glNamedFramebufferDrawBuffers(framebuffer, 3, outputs);
    const int entity = -1;
    const float depth = 1;
    const float normal[] = {.25f, .5f, .75f, 1};
    glClearTexImage(attachments[1], 0, GL_RED_INTEGER, GL_INT, &entity);
    glClearTexImage(attachments[2], 0, GL_RGBA, GL_FLOAT, normal);
    glClearTexImage(attachments[3], 0, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, 4, 4);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    IBLPrecompute::RenderSkybox(glm::mat4(1), glm::perspective(glm::radians(60.0f), 1.0f, .1f, 100.0f));
    int entities[16];
    float normals[64];
    glGetTextureImage(attachments[1], 0, GL_RED_INTEGER, GL_INT, sizeof(entities), entities);
    glGetTextureImage(attachments[2], 0, GL_RGBA, GL_FLOAT, sizeof(normals), normals);
    bool preserved = true;
    for (int i = 0; i < 16; ++i) preserved &= entities[i] == -1;
    for (int i = 0; i < 64; ++i) preserved &= normals[i] == normal[i % 4];
    std::cout << "Skybox first entity=" << entities[0] << " normal=" << normals[0] << '\n';
    Check(preserved, "skybox leaves entity and normal attachments untouched");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(4, attachments);
}

int main()
{
    Log::Init();
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(32, 32, "IBLTests", nullptr, nullptr);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return 1;
    s_GenVertexArrays = glad_glGenVertexArrays;
    s_GenBuffers = glad_glGenBuffers;
    glad_glGenVertexArrays = TrackVertexArrays;
    glad_glGenBuffers = TrackBuffers;

    IBLPrecompute::Init("../bin-int/Tests/missing-ibl-file.hdr");
    Check(!IBLPrecompute::IsReady(), "missing file leaves IBL unavailable");
    IBLPrecompute::Shutdown();
    CheckReleased();

    // 常量 RGBE 图的线性 RGB 为 (2, 1, 0.5)，可直接核对卷积前后颜色。
    const std::filesystem::path path = "../bin-int/Tests/constant-ibl.hdr";
    {
        std::ofstream out(path, std::ios::binary);
        out << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
        const unsigned char pixel[] = {128, 64, 32, 130};
        for (int i = 0; i < 4; ++i) out.write(reinterpret_cast<const char*>(pixel), 4);
    }

    // 预计算不应继承上一轮绘制的剔除、混合、深度和裁剪状态。
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT_AND_BACK);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_ZERO);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_NEVER);
    glDepthMask(GL_FALSE);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 1, 1);
    glViewport(2, 3, 13, 17);
    IBLPrecompute::Reload(path.string());
    Check(IBLPrecompute::IsReady(), "constant HDR precomputation completes");
    const auto textures = IBLPrecompute::GetTextures();
    CheckCube(textures.EnvCubemap, 0, {2, 1, .5f}, .015f);
    CheckCube(textures.IrradianceMap, 0, {2, 1, .5f}, .04f);
    for (int mip = 0; mip < 5; ++mip)
        CheckCube(textures.PrefilterMap, mip, {2, 1, .5f}, .025f);
    std::vector<float> brdf(512 * 512 * 2);
    glGetTextureImage(textures.BRDFLUT, 0, GL_RG, GL_FLOAT, GLsizei(brdf.size() * sizeof(float)), brdf.data());
    bool finite = true;
    for (float value : brdf) finite &= std::isfinite(value) && value >= 0 && value <= 1.01f;
    Check(finite && brdf[(256 * 512 + 256) * 2] > .1f, "BRDF integration is finite and populated");
    GLint viewport[4], depthFunc;
    GLboolean depthMask;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    Check(viewport[0] == 2 && viewport[1] == 3 && viewport[2] == 13 && viewport[3] == 17,
          "precomputation restores viewport");
    Check(glIsEnabled(GL_CULL_FACE) && glIsEnabled(GL_BLEND) && glIsEnabled(GL_DEPTH_TEST)
          && glIsEnabled(GL_SCISSOR_TEST) && depthFunc == GL_NEVER && depthMask == GL_FALSE,
          "precomputation restores caller draw state");
    CheckSkyboxAttachments();
    IBLPrecompute::Shutdown();
    CheckReleased();
    Check(!glIsTexture(textures.EnvCubemap) && !glIsTexture(textures.IrradianceMap)
          && !glIsTexture(textures.PrefilterMap) && !glIsTexture(textures.BRDFLUT),
          "IBL output textures released before context destruction");

    // stbi_loadf 也能读取四通道文件；上传布局仍须固定为三个 RGB 浮点分量。
    const std::filesystem::path rgbaPath = "../bin-int/Tests/rgba-ibl.tga";
    {
        std::ofstream out(rgbaPath, std::ios::binary);
        unsigned char header[18]{};
        header[2] = 2; header[12] = 2; header[14] = 2; header[16] = 32; header[17] = 8;
        out.write(reinterpret_cast<const char*>(header), sizeof(header));
        const unsigned char pixel[] = {64, 128, 255, 32}; // TGA 的 BGRA 顺序。
        for (int i = 0; i < 4; ++i) out.write(reinterpret_cast<const char*>(pixel), 4);
    }
    IBLPrecompute::Reload(rgbaPath.string());
    CheckCube(IBLPrecompute::GetTextures().EnvCubemap, 0,
              {1, std::pow(128.0f / 255, 2.2f), std::pow(64.0f / 255, 2.2f)}, .01f);
    IBLPrecompute::Shutdown();
    CheckReleased();
    Check(glGetError() == GL_NO_ERROR, "no OpenGL error");
    glfwDestroyWindow(window);
    glfwTerminate();
    return s_Failures ? 1 : 0;
}
