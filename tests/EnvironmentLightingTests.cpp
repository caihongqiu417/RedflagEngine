#include "rfpch.h"
#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Redflag/Renderer/Mesh.h"
#include "Redflag/Renderer/EnvironmentLighting.h"
#include "Redflag/Renderer/ScreenSpace/ScreenEffects.h"
#include "Redflag/Renderer/GI/GlobalIllumination.h"
#include "Redflag/Renderer/IBLPrecompute.h"
#include "RenderDocCapture.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image_write.h>

using namespace Redflag;
namespace fs = std::filesystem;
using Image = std::vector<glm::vec4>;

static void Check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

static size_t SpecularStart(int level)
{
    size_t count = 6 * IBLTextures::DiffuseSize * IBLTextures::DiffuseSize;
    for (int i=0; i<level; ++i)
    {
        size_t width = IBLTextures::ReflectionSize >> i;
        count += 6*width*width;
    }
    return count;
}

static void MakeSky(const fs::path& path)
{
    // 六个坐标轴各有可预测的颜色，+X 方向另设一小片亮窗，用来检查粗糙材质上的倒影。
    const int w=256, h=128;
    std::vector<glm::vec3> pixels(w*h);
    for (int y=0; y<h; ++y)
        for (int x=0; x<w; ++x)
        {
            float longitude = ((x+.5f)/w-.5f)*glm::two_pi<float>();
            float latitude = (.5f-(y+.5f)/h)*glm::pi<float>();
            glm::vec3 d{std::cos(latitude)*std::cos(longitude),std::sin(latitude),std::cos(latitude)*std::sin(longitude)};
            pixels[y*w+x] = glm::vec3(1)+.6f*d + (d.x > .96f ? glm::vec3(5.f):glm::vec3(0.f));
        }
    Check(stbi_write_hdr(path.string().c_str(),w,h,3,&pixels[0].x)!=0,"Cannot save HDR input");
}

static void Save(const fs::path& path, const Image& pixels, int w, int h)
{
    std::vector<unsigned char> image(w*h*3);
    for (size_t i=0; i<pixels.size(); ++i)
        for (int c=0; c<3; ++c)
        {
            float v = std::max(0.f,pixels[i][c]);
            image[3*i+c] = static_cast<unsigned char>(std::lround(255*std::pow(v/(1+v),1.f/2.2f)));
        }
    Check(stbi_write_png(path.string().c_str(),w,h,3,image.data(),w*3)!=0,"Cannot save preview");
}

#include "SceneReflectionCheck.h"
#include "RoughReflectionCheck.h"
#include "ReflectionFrameReference.h"

static std::vector<GISurface> MaterialSpheres(int width, int height, glm::vec2 sample)
{
    std::vector<GISurface> surfaces(width*height);
    int tile = width/4;
    float scale = 10.f/width;
    for (int y=0; y<height; ++y)
        for (int x=0; x<width; ++x)
        {
            int sphere=x/tile;
            glm::vec2 disk((x%tile+sample.x-.5f*tile)/(.45f*tile),(.5f*height-y-sample.y)/(.45f*tile));
            if (glm::dot(disk,disk)>=1.f) continue;
            glm::vec3 n(disk,std::sqrt(1.f-glm::dot(disk,disk)));
            surfaces[y*width+x] = {glm::vec4((x+sample.x)*scale-5.f,(.5f*height-y-sample.y)*scale,-3+n.z,1),
                {n,sphere%2 ? .85f : .02f},{.85f,.48f,.19f,sphere<2 ? 1.f:0.f}};
        }
    return surfaces;
}

static void RenderFigure(VulkanComputeDevice& device, const Ref<ComputeResource>& sky, const fs::path& path)
{
    constexpr int width=960, height=320;
    auto projection=glm::ortho(-5.f,5.f,-5.f*height/width,5.f*height/width,.01f,50.f);
    GlobalIllumination gi(device,std::vector<GIModel>{{}},width,height);
    gi.SetRasterCamera(projection,{0,0,0},true);
    ScreenEffects effects(device,width,height);
    effects.SetEnvironment(sky);
    effects.GetSettings().TraceReflections=false;
    Image accumulation(width*height,glm::vec4(0)), pixels(accumulation);
    auto black=device.CreateBuffer(pixels.size()*sizeof(pixels[0]),pixels.data());

    // 在最终图的每个像素内取四个位置，分别求球面法线并运行实际材质着色器，在线性光下合成边缘。
    for (glm::vec2 sample : {glm::vec2(.25f,.25f),glm::vec2(.75f,.25f),glm::vec2(.25f,.75f),glm::vec2(.75f,.75f)})
    {
        auto geometry=MaterialSpheres(width,height,sample);
        device.Upload(gi.GetSurfaces(),geometry.data(),geometry.size()*sizeof(geometry[0]));
        auto output=effects.Render(ScreenEffect::MaterialReflections,gi,projection,glm::mat4(1),0,black);
        device.Readback(output,pixels.data(),pixels.size()*sizeof(pixels[0]));
        for (size_t i=0; i<pixels.size(); ++i) accumulation[i]+=.25f*pixels[i];
    }
    Save(path,accumulation,width,height);
}

static void Test(VulkanContext& context, VulkanComputeDevice& device, const fs::path& out, RenderDocCapture& capture)
{
    std::ofstream result(out/"measurements.csv");
    result << "case,r,g,b,a\n";
    auto measure = [&](const char* name, glm::vec4 value) {
        result << name << ',' << value.r << ',' << value.g << ',' << value.b << ',' << value.a << '\n';
        std::cout << name << ": " << value.r << ", " << value.g << ", " << value.b << ", " << value.a << '\n';
    };

    auto baked = EnvironmentLighting::Load(device,"bin-int/Tests/test-room.rfibl");
    auto constantPath = out / L"恒定天空.HDR";
    fs::copy_file("bin-int/Tests/constant-ibl.hdr",constantPath,fs::copy_options::overwrite_existing);
    auto constant = EnvironmentLighting::Load(device,constantPath);
    Check(baked && constant,"Run tests/run_ibl_tests.cmd to prepare the OpenGL archive");
    Check(!EnvironmentLighting::Load(device,"bin-int/Tests/incomplete.rfibl"),"Truncated archive accepted");
    MakeSky(out/"window.hdr");
    auto sky = EnvironmentLighting::Load(device,out/"window.hdr");
    size_t lut = SpecularStart(IBLTextures::ReflectionLevels);
    auto read = [&](const Ref<ComputeResource>& data, size_t count) {
        Image pixels(count);
        device.Readback(data,pixels.data(),count*sizeof(pixels[0]));
        return pixels;
    };
    const size_t count = lut + size_t(IBLTextures::LutSize)*IBLTextures::LutSize;
    auto gpu = read(constant,count), gl = read(baked,count), window = read(sky,count);
    float cubeError = 0, lutMean = 0;
    for (size_t i=0; i<lut; ++i)
        cubeError = std::max(cubeError,glm::length(glm::vec3(gpu[i]-gl[i])));
    for (size_t i=lut; i<count; ++i) lutMean += glm::length(glm::vec2(gpu[i]-gl[i])) / float(count-lut);
    measure("OpenGL_vs_Vulkan_max_cube_and_mean_BRDF_error",{cubeError,lutMean,0,0});
    Check(cubeError < .007f && lutMean < .01f,"Raw HDR differs from the OpenGL constant environment");
    for (int face=0; face<6; ++face)
    {
        const int w=IBLTextures::ReflectionSize;
        auto sample = window[SpecularStart(0)+(face*w+w/2)*w+w/2];
        glm::vec3 expected(1);
        expected[face/2] += face%2 ? -.6f : .6f;
        if (face==0) expected += 5.f;
        Check(glm::length(glm::vec3(sample)-expected)<.10f,"Cube face orientation mismatch");
    }
    auto center = [&](int mip) {
        int w=IBLTextures::ReflectionSize >> mip;
        return window[SpecularStart(mip)+(w/2)*w+w/2];
    };
    measure("window_sharp",center(0));
    measure("window_rough",center(4));
    Check(center(0).r>center(4).r+2.f,"Roughness did not spread out the bright window");

    constexpr int width=160, height=64;
    const auto projection = glm::ortho(-5.f,5.f,-2.f,2.f,.01f,50.f);
    const glm::mat4 view(1);
    GlobalIllumination gi(device,std::vector<GIModel>{{}},width,height);
    gi.SetRasterCamera(projection,{0,0,0},true);
    ScreenEffects effects(device,width,height);
    auto surfaces=MaterialSpheres(width,height,glm::vec2(.5f));
    Image input(width*height,glm::vec4(0,0,0,1));
    auto inputBuffer = device.CreateBuffer(input.size()*sizeof(input[0]),input.data());
    auto draw = [&](ScreenEffect effect, const Ref<ComputeResource>& env, bool trace) {
        effects.SetEnvironment(env);
        effects.GetSettings().TraceReflections = trace;
        return read(effects.Render(effect,gi,projection,view,0,inputBuffer),width*height);
    };
    auto upload = [&] { device.Upload(gi.GetSurfaces(),surfaces.data(),surfaces.size()*sizeof(surfaces[0])); };

    upload();
    auto spec = draw(ScreenEffect::MaterialReflections,constant,false);
    auto diffuse = draw(ScreenEffect::EnvironmentDiffuse,constant,false);
    int metal = 32*width+20, paint = metal+80;
    measure("metal_specular_constant",spec[metal]); measure("dielectric_specular_constant",spec[paint]);
    measure("metal_diffuse",diffuse[metal]); measure("dielectric_diffuse",diffuse[paint]);
    Check(diffuse[metal].r==0 && diffuse[paint].r>1.5f,"Incorrect diffuse energy split");
    Check(std::abs(spec[metal].r-1.7f)<.02f && std::abs(spec[paint].r-.08f)<.005f,"Incorrect F0 or BRDF orientation");
    auto spheres = draw(ScreenEffect::MaterialReflections,sky,false);
    RenderFigure(device,sky,out/"material-spheres.png");
    Check(glm::length(glm::vec3(spheres[metal]-spheres[metal+40]))>.08f,"Material roughness did not affect output");

    // +X 与 +Z 两面的公共边。分别从边的两侧看，相邻采样应该连续。
    for (int i=0; i<2; ++i)
    {
        glm::vec3 reflection = glm::normalize(glm::vec3(1.f,0,1.f + (i==0 ? -.0002f : .0002f)));
        glm::vec3 normal = glm::normalize(reflection + glm::vec3(0,0,1));
        surfaces[i] = {{0,0,-3,1},{normal,.0f},{1,1,1,1}};
    }
    upload();
    auto edge = draw(ScreenEffect::MaterialReflections,sky,false);
    measure("cube_edge_left",edge[0]); measure("cube_edge_right",edge[1]);
    Check(glm::length(glm::vec3(edge[0]-edge[1]))<.005f,"Adjacent cube faces left a seam");

    // 两片朝右反射的金属。第一片的光线能碰到绿墙，第二片在同一方向出了画面。
    std::fill(surfaces.begin(),surfaces.end(),GISurface{});
    for (int y=0; y<height; ++y)
        for (int x=0; x<width; ++x)
        {
            const bool mirror = x==32 || x==128;
            if (!mirror && (x<53 || x>80)) continue;
            size_t i = y*width+x;
            surfaces[i] = {{(x+.5f)/16.f-5,2-(y+.5f)/16.f,mirror?-3.f:-4.f,1},
                {mirror ? glm::vec3(.8660254f,0,.5f) : glm::vec3(0,0,1),.02f},{.85f,.48f,.19f,1}};
            if (!mirror) input[i] = {0,.8f,.15f,1};
        }
    upload();
    device.Upload(inputBuffer,input.data(),input.size()*sizeof(input[0]));
    capture.Begin();
    auto both = draw(ScreenEffect::MaterialReflections,constant,true);
    capture.End();
    auto ssr = draw(ScreenEffect::MaterialReflections,{},true);
    auto ibl = draw(ScreenEffect::MaterialReflections,constant,false);
    int hit = 32*width+32, miss = 32*width+128;
    measure("SSR_hit_with_environment",both[hit]); measure("SSR_hit_without_environment",ssr[hit]);
    measure("SSR_miss_with_environment",both[miss]); measure("SSR_miss_without_environment",ssr[miss]);
    Check(both[hit].a==1 && both[hit].g>.2f && both[hit]==ssr[hit],"Screen hit added environment twice");
    Check(both[miss].a==0 && both[miss].r>1.f && both[miss]==ibl[miss] && ssr[miss].r==0,"Offscreen miss lost environment");

    // 在屏幕反射光线前方放一个红灯，仍让画面里先看到的绿墙提供颜色。
    GIModel red;
    red.Transform = glm::scale(glm::translate(view, {-1.93f,0,-3.6f}), {.5f,4.f,.5f});
    red.MaterialEmissions = {{4,0,0}};
    GlobalIllumination redScene(device, std::vector<GIModel>{red}, width, height);
    redScene.SetRasterCamera(projection, {0,0,0}, true);
    device.Upload(redScene.GetSurfaces(), surfaces.data(), surfaces.size() * sizeof(surfaces[0]));
    effects.GetSettings().TraceReflections = true;
    effects.GetSettings().TraceScene = true;
    effects.SetEnvironment({});
    auto traceGreen = [&] {
        return read(effects.Render(ScreenEffect::MaterialReflections, redScene, projection, view, 0, inputBuffer), width * height);
    };
    glm::vec4 preferred = traceGreen()[hit];
    measure("screen_green_before_scene_red", preferred);
    Check(preferred == ssr[hit], "Scene hit replaced a valid screen reflection");
    std::vector<GISurface> withoutWall = surfaces;
    for (size_t i = 0; i < withoutWall.size(); ++i)
        if (input[i].g > 0.f) withoutWall[i] = GISurface{};
    device.Upload(redScene.GetSurfaces(), withoutWall.data(), withoutWall.size() * sizeof(withoutWall[0]));
    glm::vec4 behindGreen = traceGreen()[hit];
    measure("scene_red_after_screen_removed", behindGreen);
    Check(behindGreen.r > 3 && behindGreen.g == 0.f, "Screen-priority fixture never reached the red model");
    effects.GetSettings().TraceScene = false;

    // 把两个镜面之间的绿墙换成红灯牌。先画灯牌并加进本帧光照，再让同一条屏幕反射光线找它。
    VulkanDraw sign(context, VulkanDraw::Output::Emission);
    VulkanDraw opaque(context, VulkanDraw::Output::LightingSurfaces);
    opaque.Resize(width, height);
    sign.UseSceneDepth(opaque);
    float left = 2.f * 53 / width - 1, right = 2.f * 81 / width - 1;
    std::vector<MeshVertex> corners{{{left,-1,0}}, {{right,-1,0}}, {{right,1,0}}, {{left,1,0}}};
    auto quad = sign.CreateMesh(corners, {0,1,2,2,3,0});
    SurfaceMaterial lightStrip;
    lightStrip.Emission = {4,.48f,.14f};
    auto drawSign = [&](CommandList& cmd) {
        cmd.DrawIndexed(quad, {}, glm::mat4(1), glm::mat4(1), glm::vec4(1), 1, lightStrip);
    };
    opaque.DrawSurfaces(drawSign);
    sign.DrawSurfaces(drawSign);
    std::fill(input.begin(), input.end(), glm::vec4(0,0,0,1));
    device.Upload(inputBuffer, input.data(), input.size() * sizeof(input[0]));
    static const uint32_t emissionShader[] =
#include "../RedflagForge/src/Shaders/AddEmission.comp.inc"
    ;
    auto add = device.CreatePipeline(emissionShader, sizeof(emissionShader),
                                     {ComputeBinding::Buffer, ComputeBinding::Image, ComputeBinding::Buffer, ComputeBinding::Image}, 0);
    auto sceneColor = device.CreateBuffer(input.size() * sizeof(input[0]));
    device.Dispatch(add, {inputBuffer,sign.GetEmission(),sceneColor,opaque.GetAlbedo()}, nullptr, width/8,height/8);
    effects.SetEnvironment({});
    effects.GetSettings().TraceReflections = true;
    auto mirror = read(effects.Render(ScreenEffect::MaterialReflections,gi,projection,view,0,sceneColor),width*height);
    measure("SSR_hit_emissive_sign",mirror[hit]);
    measure("SSR_miss_emissive_sign",mirror[miss]);
    Check(mirror[hit].a == 1 && mirror[hit].r > 3 && mirror[hit].g > .1f && mirror[hit].b > 0 &&
          mirror[miss] == glm::vec4(0), "Reflection did not read the lit sign from the current frame");
}

int main(int argc, char** argv)
{
    fs::path out = argc>1 ? argv[1] : "tmp/environment-lighting";
    fs::create_directories(out);
    const bool captureMirror = argc > 3 && std::string(argv[3]) == "--scene-reflections";
    const bool frameReference = argc == 3 && std::string(argv[2]) == "--frame-reference";
    const bool rough = argc >= 3 && std::string(argv[argc-1]) == "--rough-reflections";
    const char* dll = argc > 2 && !frameReference && !(rough && argc == 3) ? argv[2] : nullptr;
    RenderDocCapture capture(dll, out / (rough ? "Rough" : captureMirror ? "Mirror" : "IBL-SSR"),
        rough ? "Rough mirror / one ray and spatial filter" : captureMirror ? "Behind-camera reflection" : "Vulkan IBL and SSR",
        rough ? "96x96. The same frame is read back before and after the reflection filter." :
        captureMirror ? "96x96 polished mirror. The striped emissive sign is behind the camera. Linear RGB is read back after reflection." :
        "160x64 synthetic mirrors. Pixel (32,32) reflects the green wall; (128,32) misses it and reflects the linear HDR environment.");
    Log::Init();
    if (!glfwInit()) return 1;
    int result=0;
    try
    {
        glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        std::unique_ptr<GLFWwindow,decltype(&glfwDestroyWindow)> window(glfwCreateWindow(32,32,"IBL",nullptr,nullptr),glfwDestroyWindow);
        VulkanContext context(window.get()); context.Init();
        VulkanComputeDevice device(context);
        if (frameReference) BuildReflectionReference(device, out);
        else if (rough) CheckRoughReflections(device, out, capture);
        else
        {
            if (!captureMirror) Test(context,device,out,capture);
            CheckSceneReflections(device, out, captureMirror ? &capture : nullptr);
        }
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result=1; }
    glfwTerminate();
    return result;
}
