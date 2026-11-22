#include "rfpch.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Renderer/Framebuffer.h"
#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Renderer3D.h"
#include "Redflag/Renderer/UniformBuffer.h"
#include "RenderDocCapture.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image_write.h>

using namespace Redflag;
namespace fs = std::filesystem;
constexpr int W = 384, H = 216;
const fs::path Out = "../output/profiling/gl-transparency";
using Image = std::vector<glm::vec4>;

static void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void Save(const char* file, const Image& pixels)
{
    std::vector<unsigned char> rgb(W*H*3);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            for (int c = 0; c < 3; ++c)
            {
                float linear = pixels[(H-1-y)*W+x][c];
                float exposed = std::max(linear, 0.f) / (1.f + std::max(linear, 0.f));
                rgb[(y*W+x)*3+c] = static_cast<unsigned char>(std::lround(255 * std::pow(exposed, 1/2.2f)));
            }
    Check(stbi_write_png((Out/file).string().c_str(), W, H, 3, rgb.data(), 3*W) != 0, "write PNG");
}

static float Difference(const Image& a, const Image& b)
{
    float largest = 0;
    for (size_t i = 0; i < a.size(); ++i)
        for (int c = 0; c < 3; ++c)
        {
            Check(std::isfinite(a[i][c]) && std::isfinite(b[i][c]), "non-finite HDR result");
            largest = std::max(largest, std::abs(a[i][c]-b[i][c]));
        }
    return largest;
}

static void Run(RenderDocCapture& capture)
{
    FramebufferSpecification spec;
    spec.Width = W;
    spec.Height = H;
    spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER,
        FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH32F };
    auto target = Framebuffer::Create(spec);
    std::vector<MeshVertex> vertices = {
        {{-1,-1,0}, {0,0,1}, {0,0}, {1,0,0}}, {{1,-1,0}, {0,0,1}, {1,0}, {1,0,0}},
        {{1,1,0}, {0,0,1}, {1,1}, {1,0,0}}, {{-1,1,0}, {0,0,1}, {0,1}, {1,0,0}} };
    auto quad = Mesh::Create(vertices,{0,1,2, 2,3,0});
    auto back = Mesh::Create(vertices,{0,2,1, 2,0,3});

    auto grid = Texture2D::Create(W,H);
    std::vector<unsigned char> texels(W*H*4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            auto color = (x % 24 < 3 || y % 24 < 3) ? glm::ivec3(55,75,90)
                : ((x/24 + y/24) % 2 ? glm::ivec3(193,173,122) : glm::ivec3(110,160,171));
            for (int c = 0; c < 3; ++c) texels[(y*W+x)*4+c] = color[c];
            texels[(y*W+x)*4+3] = 255;
        }
    grid->SetData(texels.data(), static_cast<uint32_t>(texels.size()));
    auto wall = Material::Create(grid);
    auto post = Material::Create(glm::vec4(.15f,.60f,.24f,1));
    auto rose = Material::Create(glm::vec4(.92f,.095f,.26f,.37f));
    auto blue = Material::Create(glm::vec4(.12f,.48f,.95f,.29f));
    rose->Alpha = blue->Alpha = AlphaMode::Blend;

    std::array<unsigned char,16+17*64> lightData{};
    int count = 1;
    glm::vec4 direction{0,0,-1,0}, intensity{4.5f,4.5f,4.5f,1};
    std::memcpy(lightData.data(),&count,4);
    std::memcpy(lightData.data()+16,&direction,16);
    std::memcpy(lightData.data()+32,&intensity,16);
    auto lights = UniformBuffer::Create(static_cast<uint32_t>(lightData.size()),3);
    lights->SetData(lightData.data(), static_cast<uint32_t>(lightData.size()));

    const glm::vec3 eye{0,0,5};
    auto view = glm::lookAt(eye,glm::vec3(0),glm::vec3(0,1,0));
    auto vp = glm::ortho(-2.f,2.f,-1.125f,1.125f,.1f,12.f) * view;
    auto plane = [](glm::vec3 center,float angle,glm::vec2 halfSize) {
        return glm::scale(glm::rotate(glm::translate(glm::mat4(1),center),glm::radians(angle),glm::vec3(0,1,0)),
            glm::vec3(halfSize,1));
    };

    std::vector<int> ids(W*H);
    std::vector<float> depth(W*H);
    auto render = [&](int panes, bool reverse, bool backside, bool record = false) {
        target->Bind();
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        target->ClearAttachment(1,-1);
        Renderer3D::BeginScene(vp,eye,view);
        Renderer3D::DrawMesh(quad,plane({0,0,-2},0,{2,1.125}),wall,41);
        Renderer3D::DrawMesh(quad,plane({.35f,-.25f,1.3f},0,{.09,.64}),post,52);
        auto pane = [&](bool second) {
            if (second) Renderer3D::DrawMesh(backside ? back : quad,plane({.47f,-.06f,-.35f},-30,{.9f,.72f}),blue,63);
            else Renderer3D::DrawMesh(backside ? back : quad,plane({-.42f,.1f,.18f},36,{.95f,.73f}),rose,62);
        };
        if (panes) pane(reverse);
        if (panes == 2) pane(!reverse);

        if (record) capture.Begin();
        Renderer3D::EndScene();
        if (record) capture.End();
        glReadBuffer(GL_COLOR_ATTACHMENT1);
        glReadPixels(0,0,W,H,GL_RED_INTEGER,GL_INT,ids.data());
        glReadPixels(0,0,W,H,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
        Image color(W*H);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0,0,W,H,GL_RGBA,GL_FLOAT,color.data());
        Check(glGetError() == GL_NO_ERROR,"OpenGL error in weighted transparency");
        return color;
    };

    fs::create_directories(Out);
    auto empty = render(0,false,false);
    auto wallIDs = ids;
    auto wallDepth = depth;
    auto straight = render(2,false,false);
    rose->Thickness = blue->Thickness = .45f;
    auto equalIOR = render(2,false,false);
    Check(Difference(straight,equalIOR) == 0,"IOR=1 shifted background");
    rose->RefractionIndex = blue->RefractionIndex = 1.52f;
    rose->Thickness = blue->Thickness = 0;
    Check(Difference(straight,render(2,false,false)) == 0,"zero thickness shifted background");
    rose->Thickness = blue->Thickness = .45f;
    auto refracted = render(2,false,false,true);
    auto reversed = render(2,true,false);
    float orderError = Difference(refracted,reversed);
    Check(orderError == 0,"submission order changed output");
    Check(wallIDs == ids && wallDepth == depth, "glass altered pick IDs or depth");
    Check(Difference(refracted,straight) > .03f,"refraction did not move background");

    size_t visiblePost = 0;
    for (size_t i = 0; i < ids.size(); ++i)
        if (ids[i] == 52)
        {
            Check(refracted[i] == empty[i],"glass drew over a nearer opaque surface");
            ++visiblePost;
        }
    Check(visiblePost > 100,"missing foreground post");
    // 只改变前景柱的颜色。折射到柱子轮廓的玻璃像素也不应沾上柱子的颜色。
    const auto originalPost = post->BaseColor;
    float leaked = 0;
    for (float thickness : {.16f, .32f, .45f, .60f})
    {
        rose->Thickness = blue->Thickness = thickness;
        post->BaseColor = originalPost;
        auto greenPost = render(2,false,false);
        post->BaseColor = glm::vec4(.9f,.025f,.85f,1);
        auto brightPost = render(2,false,false);
        for (size_t i = 0; i < wallIDs.size(); ++i)
            if (wallIDs[i] != 52)
                for (int c = 0; c < 3; ++c)
                    leaked = std::max(leaked,std::abs(brightPost[i][c]-greenPost[i][c]));
    }
    rose->Thickness = blue->Thickness = .45f;
    if (leaked > 0) std::cerr << "Max foreground color leak (linear RGB): " << leaked << '\n';
    Check(leaked == 0, "foreground edge leaked into refracted background");
    post->BaseColor = originalPost;
    Check(Difference(empty,render(1,false,true)) == 0,"one-sided glass showed its back face");
    rose->DoubleSided = true;
    Check(Difference(empty,render(1,false,true)) > .03f,"two-sided glass lost its back face");
    rose->DoubleSided = false;
    rose->BaseColor.a = 0;
    Check(Difference(empty,render(1,false,false)) == 0,"alpha=0 changed opaque background");

    // 编辑器的深度纹理、带模板的多重采样帧缓冲以及直接绘制到窗口共用这个入口。
    rose->BaseColor.a = .37f;
    auto checkTarget = [&] {
        glDepthMask(GL_TRUE);
        glClearColor(.3f,.45f,.25f,1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ZERO);
        glDepthFunc(GL_GREATER);
        Renderer3D::BeginScene(vp,eye,view);
        Renderer3D::DrawMesh(quad,plane({0,0,0},18,{1,1}),rose,62);
        Renderer3D::EndScene();
        GLint blend, depthFunction;
        glGetIntegeri_v(GL_BLEND_SRC_RGB,0,&blend);
        glGetIntegerv(GL_DEPTH_FUNC,&depthFunction);
        Check(glIsEnabledi(GL_BLEND,0) && blend == GL_SRC_ALPHA && depthFunction == GL_GREATER,
            "transparent pass leaked its blend or depth state");
        Check(glGetError() == GL_NO_ERROR,"window or multisample GL target");
    };
    spec.Samples = 4;
    spec.Attachments = {FramebufferTextureFormat::RGBA8,FramebufferTextureFormat::DEPTH24STENCIL8};
    auto msaa = Framebuffer::Create(spec);
    msaa->Bind();
    checkTarget();
    msaa->Unbind();
    glViewport(0,0,W,H);
    checkTarget();

    Save("opaque.png",empty);
    Save("straight.png",straight);
    Save("refracted.png",refracted);
    std::ofstream out(Out/"measurements.csv");
    out << "test,value\nwidth," << W << "\nheight," << H << "\norder_max_abs_linear," << orderError
        << "\nior_one_max_abs_linear,0\nzero_thickness_max_abs_linear,0\nrefraction_max_abs_linear," << Difference(refracted,straight)
        << "\nvisible_opaque_post_pixels," << visiblePost << "\nforeground_color_leak," << leaked << "\nchanged_pick_ids,0\nchanged_depth_pixels,0\n";
}

int main(int argc, char** argv)
{
    try
    {
        Log::Init();
        RenderDocCapture capture(argc > 1 ? argv[1] : nullptr, Out/"GL-Glass", "Two glass sheets, OpenGL",
            "384x216 orthographic grid, pink and blue transparent sheets, opaque foreground post. Weighted OIT + screen-space refraction.");
        Check(glfwInit() == GLFW_TRUE,"GLFW init");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,6);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        auto window = glfwCreateWindow(W,H,"Glass",nullptr,nullptr);
        Check(window != nullptr,"OpenGL context");
        glfwMakeContextCurrent(window);
        Check(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress) != 0,"GL loader");
        RenderCommand::Init();
        Renderer3D::Init();
        Run(capture);
        Renderer3D::Shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cout << "OpenGL weighted transparency checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
