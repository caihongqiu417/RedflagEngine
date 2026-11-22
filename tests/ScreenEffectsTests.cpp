#include "rfpch.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "Redflag/Renderer/GI/GlobalIllumination.h"
#include "Redflag/Renderer/ScreenSpace/ScreenEffects.h"
#include "RenderDocCapture.h"
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>

using namespace Redflag;
namespace fs=std::filesystem;
#include "PointMaterialCheck.h"
#include "CubeMaterialCheck.h"
#include "DirectionalLightCheck.h"

static std::vector<GILight> ClusterLights()
{
    std::vector<GILight> lights;
    for (int i=0;i<49;++i)
        lights.push_back({{float(i%7)*2.6f-8.f,-1.2f,float(i/7)*1.4f-4.f,1.f},
            {.4f+float(i%3),.5f+float(i%5),.7f+float(i%7),0}});
    return lights;
}

static void Run(VulkanComputeDevice& device,const fs::path& model,const fs::path& output,RenderDocCapture* capture)
{
    constexpr uint32_t w=161,h=91; // 也覆盖不足一个工作组的右边缘和底边。
    constexpr size_t pixels=w*h;
    GlobalIllumination gi(device,model,w,h);
    ScreenEffects effects(device,w,h);
    const glm::vec3 eye(-5.5f,-2.2f,0), target(6,-1.8f,0);
    auto view=glm::lookAt(eye,target,glm::vec3(0,1,0));
    auto projection=glm::perspective(glm::radians(65.f),float(w)/h,.05f,100.f);

    // 前四项读同一机位的表面。提前算一次亚像素偏移图，让 TAA 有上一帧可读。
    if (capture) {
        const auto jitter=ScreenEffects::JitterProjection(projection,0,w,h);
        gi.SetCamera(jitter*view,eye);
        gi.Render(GIMethod::DirectReference);
        effects.Render(ScreenEffect::TAA,gi,jitter,view,0,{},&projection);
        gi.SetCamera(projection*view,eye);
        gi.Render(GIMethod::DirectReference);

        capture->Begin();
        effects.Render(ScreenEffect::GTAO,gi,projection,view,0);
        effects.GetSettings().Roughness=.08f;
        effects.Render(ScreenEffect::StochasticReflections,gi,projection,view,0);
        effects.Render(ScreenEffect::TAA,gi,projection,view,1);
        gi.SetLights(ClusterLights());
        effects.Render(ScreenEffect::ClusteredLighting,gi,projection,view,0);
        capture->End();
        return;
    }

    fs::create_directories(output);
    auto write=[&](const char* name,auto& values) {
        std::ofstream stream(output/(std::string(name)+".f32"),std::ios::binary);
        stream.write(reinterpret_cast<const char*>(values.data()),values.size()*sizeof(values[0]));
        if (!stream) throw std::runtime_error("Cannot save screen effect readback");
    };
    auto save=[&](const char* name,const Ref<ComputeResource>& buffer) {
        std::vector<glm::vec4> values(pixels);
        device.Readback(buffer,values.data(),pixels*sizeof(glm::vec4));
        write(name,values);
        return values;
    };

    gi.Render(GIMethod::DirectReference);
    save("direct",gi.GetOutput());
    std::vector<GISurface> surfaces(pixels);
    device.Readback(gi.GetSurfaces(),surfaces.data(),pixels*sizeof(GISurface));
    write("surfaces",surfaces);
    save("gtao",effects.Render(ScreenEffect::GTAO,gi,projection,view,0));
    effects.GetSettings().AORadius=.0001f;
    save("gtao-tiny-radius",effects.Render(ScreenEffect::GTAO,gi,projection,view,0));
    effects.GetSettings().Roughness=.08f;
    save("ssr",effects.Render(ScreenEffect::StochasticReflections,gi,projection,view,0));
    save("ssr-repeat",effects.Render(ScreenEffect::StochasticReflections,gi,projection,view,0));
    effects.GetSettings().Roughness=.65f;
    save("ssr-rough",effects.Render(ScreenEffect::StochasticReflections,gi,projection,view,0));

    // 每帧的光线经过不同的子像素位置。额外保存未混合的八张图，供离线算均值。
    for (uint32_t frame=0;frame<8;++frame) {
        auto jitter=ScreenEffects::JitterProjection(projection,frame,w,h);
        gi.SetCamera(jitter*view,eye);
        gi.Render(GIMethod::DirectReference);
        save(("jitter-"+std::to_string(frame)).c_str(),gi.GetOutput());
        auto taa=effects.Render(ScreenEffect::TAA,gi,jitter,view,frame,{},&projection);
        if (!frame || frame==7) save(frame?"taa-last":"taa-first",taa);
    }
    auto moved=glm::lookAt(eye+glm::vec3(0,0,1),target,glm::vec3(0,1,0));
    gi.SetCamera(projection*moved,eye+glm::vec3(0,0,1));
    gi.Render(GIMethod::DirectReference);
    save("taa-moved",effects.Render(ScreenEffect::TAA,gi,projection,moved,8));
    effects.ResetHistory();
    save("taa-reset",effects.Render(ScreenEffect::TAA,gi,projection,moved,9));

    // 相机静止，直接光逐灯求和。连续帧中唯一变化的输入是像素内的采样位置。
    effects.ResetHistory();
    for (uint32_t frame=0; frame<32; ++frame) {
        auto jitter=ScreenEffects::JitterProjection(projection,frame,w,h);
        gi.SetCamera(jitter*view,eye);
        gi.Render(GIMethod::DirectReference);
        save(("static-input-"+std::to_string(frame)).c_str(),gi.GetOutput());
        auto taa=effects.Render(ScreenEffect::TAA,gi,jitter,view,frame,{},&projection);
        if (frame>=16) save(("static-taa-"+std::to_string(frame)).c_str(),taa);
    }

    // 第 33 盏灯落到第二个 32 位整数。逐灯参考使用同一半径和同一漫反射公式。
    gi.SetCamera(projection*view,eye);
    auto lights=ClusterLights();
    gi.SetLights(lights);
    save("clustered",effects.Render(ScreenEffect::ClusteredLighting,gi,projection,view,0));
    std::vector<glm::vec4> reference(pixels,glm::vec4(0));
    for (size_t i=0;i<pixels;++i) if (surfaces[i].Position.w!=0.f) {
        auto& s=surfaces[i];
        glm::vec3 sum(0);
        for (auto& lamp:lights) {
            auto delta=glm::vec3(lamp.Position-s.Position);
            float r=std::max(glm::length(delta),.0001f);
            float fade=std::max(0.f,1.f-r*r/16.f);
            sum+=glm::vec3(s.Albedo)*glm::vec3(lamp.Intensity)*
                (std::max(0.f,glm::dot(glm::vec3(s.Normal),delta/r))*fade*fade/(3.141592654f*r*r));
        }
        reference[i]={sum,0};
    }
    write("cluster-reference",reference);
    gi.SetLights({});
    save("cluster-empty",effects.Render(ScreenEffect::ClusteredLighting,gi,projection,view,0));
}

// 同一盏灯照在画面右侧。透视相机的像素随深度分开，正交相机的像素沿平行线前进。
static void CheckClusterProjection(VulkanComputeDevice& device, const fs::path& output)
{
    const uint32_t width = 96, height = 48;
    const glm::mat4 view(1.0f);
    const float radius = .95f;
    const GILight lamp{{2.6f, 0.f, -2.5f, 1.f}, {1.f, .6f, .2f, radius}};
    GlobalIllumination scene(device, std::vector<GIModel>{{}}, width, height);
    scene.SetLights({lamp});
    ScreenEffects effects(device, width, height);
    bool passed = true;

    for (bool ortho : {false, true})
    {
        auto projection = ortho ? glm::ortho(-4.f,4.f,-2.f,2.f,.1f,100.f)
            : glm::perspective(glm::radians(64.f),2.f,.1f,100.f);
        scene.SetRasterCamera(projection, glm::vec3(0), true);
        std::vector<GISurface> wall(width*height);
        for (uint32_t y=0; y<height; ++y)
            for (uint32_t x=0; x<width; ++x)
            {
                glm::vec2 ndc = (glm::vec2(x+.5f,y+.5f)/glm::vec2(width,height)*2.f-1.f)*glm::vec2(1,-1);
                auto clip = projection * glm::vec4(0,0,-3,1);
                auto hit = glm::inverse(projection) * glm::vec4(ndc,clip.z/clip.w,1);
                wall[y*width+x] = {hit/hit.w, {0,0,1,.65f}, {.9f,.7f,.5f,0}};
            }
        device.Upload(scene.GetSurfaces(), wall.data(), wall.size()*sizeof(wall[0]));
        auto shaded = effects.Render(ScreenEffect::ClusteredLighting,scene,projection,view,0);
        std::vector<glm::vec4> image(wall.size());
        device.Readback(shaded,image.data(),image.size()*sizeof(image[0]));

        float largest = 0;
        size_t illuminated = 0, missed = 0;
        for (size_t i=0; i<wall.size(); ++i)
        {
            auto V = ortho ? glm::dvec3(0,0,1) : glm::normalize(-glm::dvec3(wall[i].Position));
            glm::vec3 expected = glm::vec3(ReferencePoint(wall[i],lamp,V));
            for (int c=0;c<3;++c) largest = std::max(largest,std::abs(expected[c]-image[i][c]));
            if (expected.r > .0001f)
            {
                ++illuminated;
                if (image[i].r == 0.f) ++missed;
            }
        }
        const char* name = ortho ? "orthographic" : "perspective";
        std::cout << name << " cluster max RGB error " << largest << ", missed " << missed << "/" << illuminated << '\n';
        std::ofstream raw(output/(std::string("cluster-")+name+".f32"),std::ios::binary);
        raw.write(reinterpret_cast<const char*>(image.data()),image.size()*sizeof(image[0]));
        passed &= illuminated > 0 && !missed && largest < 2.e-5f;
    }
    if (!passed) throw std::runtime_error("Cluster cells do not cover the projected wall");
}

static void CheckSurfaceTexture(ComputeDevice& device)
{
    GIModel model{"tests/fixtures/TextureQuad.gltf"};
    model.MaterialColors.push_back({.5f, .25f, 1, 1});
    GlobalIllumination scene(device, std::vector<GIModel>{model}, 10, 10);
    glm::vec3 eye(0, 0, 1);
    const auto view = glm::lookAt(eye, glm::vec3(0), glm::vec3(0, 1, 0));
    scene.SetCamera(glm::perspective(glm::radians(90.f), 1.f, .01f, 10.f) * view, eye);
    std::array<GISurface, 100> surfaces;
    device.Readback(scene.GetSurfaces(), surfaces.data(), sizeof(surfaces));

    // 四个纹素的正中心。根节点缩放、UV 朝向或子网格底色有一步错，四个结果就无法同时相等。
    const size_t points[] = {22, 27, 72, 77};
    const glm::vec3 expected[] = {{.5f,0,0},{0,.25f,0},{0,0,1},{.5f,.25f,1}};
    const glm::vec2 factors[] = {{64,255},{128,0},{192,64},{255,128}};
    for (int i = 0; i < 4; ++i)
    {
        auto& s = surfaces[points[i]];
        if (glm::length(glm::vec2(s.Normal.w,s.Albedo.w) - factors[i]*glm::vec2(.8f,.6f)/255.f) > .0001f)
            throw std::runtime_error("Wrong linear roughness/metallic at texel " + std::to_string(i));
        if (s.Position.w != 1 || glm::length(glm::vec3(s.Albedo) - expected[i]) > 1e-4f)
            throw std::runtime_error("Wrong GI texture or node transform at pixel " + std::to_string(points[i]) +
                " rgb " + std::to_string(s.Albedo.r) + " " + std::to_string(s.Albedo.g) + " " + std::to_string(s.Albedo.b));
    }
    std::cout << "4 GI texture, tint and root-transform samples match.\n";
}

int main(int argc,char** argv)
{
    const bool materials = (argc==4 || argc==5) && std::string(argv[3])=="--point-materials";
    const bool directional = (argc==4 || argc==5) && std::string(argv[3])=="--directional";
    if (argc!=3 && !materials && !directional && !(argc==5 && std::string(argv[3])=="--capture")) return 2;
    const char* path = "output/profiling/screen-effects/SponzaScreen";
    const char* title = "Sponza / screen effects";
    const char* description = "161x91. GTAO, stochastic SSR, TAA and 49-light clustered culling/shading.";
    if (materials) {
        path = "output/captures/local/PointMaterials";
        title = "GGX point lights";
        description = "33x33 orthographic, smooth metal, three point lights. Exhaustive and clustered shading.";
    }
    if (directional) {
        path = "output/validation/directional/DirectionalLight";
        title = "Directional light and shadows";
        description = "48x16 orthographic. Two receiving depths and one blocker above the left half. Direct, clustered, ReSTIR DI and fog.";
    }
    RenderDocCapture capture(argc==5?argv[4]:nullptr,path,title,description);
    Log::Init();
    if (!glfwInit()) return 1;
    int result=0;
    try {
        glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        std::unique_ptr<GLFWwindow,decltype(&glfwDestroyWindow)> window(
            glfwCreateWindow(96,64,"Screen effect tests",nullptr,nullptr),glfwDestroyWindow);
        if (!window) throw std::runtime_error("Cannot create screen effect test window");
        VulkanContext context(window.get()); context.Init();
        VulkanComputeDevice device(context);
        if (argc==5) {
            if (!context.BeginFrame()) throw std::runtime_error("Cannot acquire a capture window");
            context.BeginWindowPass();
            context.EndWindowPass();
            context.SwapBuffers();
        }
        if (!materials && !directional) Run(device,argv[1],argv[2],argc==5?&capture:nullptr);
        if (argc==3 || directional) CheckDirectionalLight(device,argv[2],capture);
        if (argc == 3 || materials)
        {
            CheckPointMaterials(device,argv[2],capture);
            CheckMetalBounce(device,argv[2]);
            CheckSurfaceTexture(device);
            CheckCubeMaterial(context, device, argv[2]);
            CheckClusterProjection(device,argv[2]);
        }
        VkPhysicalDeviceProperties gpu{};
        vkGetPhysicalDeviceProperties(context.GetPhysicalDevice(), &gpu);
        std::ofstream(fs::path(argv[2]) / "device.txt") << gpu.deviceName;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; result=1; }
    glfwTerminate();
    return result;
}
