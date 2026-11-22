#include "rfpch.h"
#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Redflag/Renderer/GI/GlobalIllumination.h"
#include "Redflag/Renderer/Mesh.h"
#include "Redflag/Renderer/ScreenSpace/ScreenEffects.h"
#include "RenderDocCapture.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image_write.h>

using namespace Redflag;
namespace fs = std::filesystem;
constexpr int Width = 128, Height = 64;
using Image = std::vector<glm::vec4>;

static glm::vec2 Pixel(const glm::mat4& vp, glm::vec4 position)
{
    auto clip = vp * position;
    return (glm::vec2(clip) / clip.w * glm::vec2(.5f,-.5f) + .5f) * glm::vec2(Width,Height);
}

static void Save(const fs::path& path, const Image& image)
{
    std::vector<unsigned char> rgb(Width*Height*3);
    for (size_t i=0; i<image.size(); ++i)
        for (int c=0; c<3; ++c)
            rgb[3*i+c] = static_cast<unsigned char>(std::lround(255*std::pow(glm::clamp(image[i][c],0.f,1.f),1.f/2.2f)));
    if (!stbi_write_png(path.string().c_str(),Width,Height,3,rgb.data(),Width*3))
        throw std::runtime_error("Cannot write TAA readback");
}

static float CheckJitter(const glm::mat4& projection)
{
    // Halton 序列开头三个点，先减去像素中心 (0.5, 0.5)。像素从左上角起算。
    const glm::vec2 expected[] = {{0,-1.f/6}, {-.25f,1.f/6}, {.25f,-7.f/18}};
    float error = 0;
    for (uint32_t frame=0; frame<3; ++frame)
    {
        auto jitter = ScreenEffects::JitterProjection(projection,frame,Width,Height);
        for (float depth : {.2f,1.f,8.f,24.f})
        {
            glm::vec4 point{depth*.02f,depth*.03f,-depth,1};
            auto shift = Pixel(jitter,point) - Pixel(projection,point);
            error = std::max(error,glm::length(shift-expected[frame]));
        }
    }
    return error;
}

static bool Run(VulkanContext& context, VulkanComputeDevice& device, const fs::path& out, RenderDocCapture& capture)
{
    static const uint32_t packCode[] =
#include "Platform/Vulkan/Shaders/PackSurfaces.comp.inc"
    ;
    auto pack = device.CreatePipeline(packCode,sizeof(packCode),
        {ComputeBinding::Image,ComputeBinding::Image,ComputeBinding::Buffer},sizeof(glm::mat4));
    VulkanDraw draw(context,VulkanDraw::Output::LightingSurfaces);
    draw.Resize(Width,Height);
    auto quad = draw.CreateMesh({
        {{-1,-1,0},{0,0,1},{0,0},{1,0,0}},{{1,-1,0},{0,0,1},{1,0},{1,0,0}},
        {{1,1,0},{0,0,1},{1,1},{1,0,0}},{{-1,1,0},{0,0,1},{0,1},{1,0,0}}
    },{0,1,2,2,3,0});
    GlobalIllumination gi(device,std::vector<GIModel>{{}},Width,Height);
    auto plain = device.CreateBuffer(Width*Height*sizeof(glm::vec4));
    auto makePlane = [](glm::vec3 center,glm::vec2 size) {
        return glm::scale(glm::translate(glm::mat4(1),center),glm::vec3(size,1));
    };
    const auto wall = makePlane({0,0,-8},{12,7});
    const auto panel = makePlane({0,0,-7},{.90f,.85f});
    const glm::vec4 blue{.1f,.25f,.8f,1}, red{.9f,.12f,.06f,1};
    std::ofstream csv(out/"measurements.csv");
    csv << "projection,disoccluded,false_history,stable_pixels_with_history,max_color_error,max_jitter_error_pixels,near_history_weight\n";
    bool passed = true;

    for (bool ortho : {false,true})
    {
        ScreenEffects effects(device,Width,Height);
        // 让正交和透视相机在蓝墙上看到同样宽的一段，两种投影就能用同一块挡板测试。
        const auto proj = ortho ? glm::ortho(-4.f,4.f,-2.f,2.f,.01f,50.f)
            : glm::perspective(2.f*std::atan(2.f/8.f),2.f,.01f,50.f);
        const std::string name = ortho ? "ortho" : "perspective";
        Image oldColor(Width*Height), current(Width*Height), filtered(Width*Height);
        std::vector<GISurface> surfaces(Width*Height);
        const auto view0 = glm::mat4(1.f);
        const glm::vec3 eye1{1.60f,0,0};
        const auto view1 = glm::rotate(glm::mat4(1),glm::radians(12.f),glm::vec3(0,1,0))
            * glm::translate(glm::mat4(1),-eye1);

        auto render = [&](const glm::mat4& view, glm::vec3 eye, uint32_t frame, bool nearWall) {
            auto vp = proj * view;
            gi.SetRasterCamera(vp,eye,false);
            draw.DrawSurfaces([&](CommandList& commands){
                const auto model = nearWall ? makePlane({0,0,-.15f},{12,7}) : wall;
                commands.DrawIndexed(quad,{},vp,model,blue,1);
                if (!nearWall) commands.DrawIndexed(quad,{},vp,panel,red,2);
            });
            auto inv = glm::inverse(vp);
            device.Dispatch(pack,{draw.GetAlbedo(),draw.GetNormalDepth(),gi.GetSurfaces()},&inv,(Width+7)/8,(Height+7)/8);
            device.Readback(draw.GetAlbedo(),current.data(),current.size()*sizeof(current[0]));
            device.Upload(plain,current.data(),current.size()*sizeof(current[0]));
            if (ortho && frame==1 && !nearWall) capture.Begin();
            const auto taa = effects.Render(ScreenEffect::TAA,gi,proj,view,frame,plain,&proj);
            if (ortho && frame==1 && !nearWall) capture.End();
            device.Readback(taa,filtered.data(),filtered.size()*sizeof(filtered[0]));
        };

        render(view0,{0,0,0},0,false);
        oldColor=current;
        render(view1,eye1,1,false);
        device.Readback(gi.GetSurfaces(),surfaces.data(),surfaces.size()*sizeof(surfaces[0]));

        size_t exposed = 0, falseHistory = 0, stillValid = 0;
        float colorError = 0;
        for (size_t i=0; i<surfaces.size(); ++i)
        {
            if (current[i].b < .6f) continue;
            auto previous = Pixel(proj,surfaces[i].Position) - .5f;
            auto p = glm::ivec2(glm::floor(previous));
            if (p.x<0 || p.y<0 || p.x+1>=Width || p.y+1>=Height) continue;
            bool allRed = true, allBlue = true;
            for (int y=0; y<2; ++y)
                for (int x=0; x<2; ++x)
                {
                    auto color = oldColor[(p.y+y)*Width+p.x+x];
                    allRed &= color.r > .7f;
                    allBlue &= color.b > .6f;
                }
            if (allBlue && filtered[i].a > .4f) ++stillValid;
            if (!allRed) continue;
            // 这一帧刚露出蓝墙，上一帧的四个像素却都来自红挡板。检查旧颜色所占的比例，应该为零。
            ++exposed;
            if (filtered[i].a > .001f) ++falseHistory;
            colorError = std::max(colorError,std::abs(filtered[i].r-current[i].r));
        }
        Save(out/(name+"-old.png"),oldColor);
        Save(out/(name+"-current.png"),current);
        Save(out/(name+"-taa.png"),filtered);

        // 把墙放在距相机 0.15 个场景单位处，再平移正交相机半个像素。两帧都看到蓝墙，TAA 应当保留上一帧的颜色。
        float nearWeight = -1.f;
        if (ortho)
        {
            effects.ResetHistory();
            render(view0,{0,0,0},0,true);
            glm::vec3 eye{4.f/Width,2.f/Height,0};
            render(glm::translate(glm::mat4(1),-eye),eye,1,true);
            nearWeight = filtered[(Height/2)*Width+Width/2].a;
        }
        float jitterError = CheckJitter(proj);
        csv << name << ',' << exposed << ',' << falseHistory << ',' << stillValid << ',' << colorError
            << ',' << jitterError << ',' << nearWeight << '\n';
        std::cout << name << ": newly exposed " << exposed << ", wrong history " << falseHistory
            << ", same-wall preserved " << stillValid << ", max red error " << colorError
            << ", jitter pixel error " << jitterError << ", near-wall history " << nearWeight << '\n';
        passed &= exposed > 5 && falseHistory==0 && stillValid>100 && colorError < 1.e-5f
            && jitterError < .0001f && (!ortho || std::abs(nearWeight-.5f) < .0001f);
    }
    return passed;
}

int main(int argc,char** argv)
{
    fs::path out = argc>1 ? argv[1] : "tmp/taa-camera";
    fs::create_directories(out);
    RenderDocCapture capture(argc>2 ? argv[2] : nullptr,out/"TAA-Ortho","Orthographic TAA",
        "Second frame, real rasterized red foreground and blue wall. Camera turns 12 degrees. Previous wall/occluder history is checked per tap.");
    Log::Init();
    if (!glfwInit()) return 1;
    bool passed = false;
    try
    {
        glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        std::unique_ptr<GLFWwindow,decltype(&glfwDestroyWindow)> window(
            glfwCreateWindow(Width,Height,"TAA Camera",nullptr,nullptr),glfwDestroyWindow);
        VulkanContext context(window.get()); context.Init();
        VulkanComputeDevice device(context);
        passed = Run(context,device,out,capture);
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; }
    glfwTerminate();
    return passed ? 0 : 1;
}
