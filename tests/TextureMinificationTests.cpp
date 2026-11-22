#include "rfpch.h"
#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Platform/Vulkan/VulkanDrawData.h"
#include "Redflag/Renderer/Mesh.h"
#include "RenderDocCapture.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image_write.h>

using namespace Redflag;
namespace fs = std::filesystem;
using Image = std::vector<glm::vec4>;
constexpr int Width=256, Height=144;

static void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void Picture(const fs::path& path, const Image& image)
{
    std::vector<uint8_t> gray(image.size());
    for (size_t i=0; i<image.size(); ++i)
    {
        float l=glm::clamp(image[i].r,0.f,1.f);
        float s=l<.0031308f ? 12.92f*l : 1.055f*std::pow(l,1.f/2.4f)-.055f;
        gray[i]=static_cast<uint8_t>(std::lround(255*s));
    }
    Check(stbi_write_png(path.string().c_str(),Width,Height,1,gray.data(),Width)!=0,"Cannot write frame");
}

static void CheckSubpixelMasks(VulkanContext& context, const fs::path& out)
{
    // 每个高分辨率样点正好覆盖四列纹素。每四列一根铁丝，理论上一个输出像素有四分之一被遮住。
    constexpr int side = 32;
    VulkanDraw draw(context, VulkanDraw::Output::LightingSurfaces);
    draw.Resize(side, side);
    draw.SetSubpixelMask(true);
    VulkanComputeDevice device(context);
    std::vector<MeshVertex> vertices{
        {{-1,-1,0},{0,0,1},{0,0}}, {{1,-1,0},{0,0,1},{1,0}},
        {{1,1,0},{0,0,1},{1,1}}, {{-1,1,0},{0,0,1},{0,1}} };
    Ref<MeshGeometry> quad = draw.CreateMesh(vertices, {0,1,2,2,3,0});
    std::vector<uint32_t> rgba(side * 4), red(side * 4), shifted(side * 4);
    for (int x = 0; x < side * 4; ++x)
    {
        rgba[x] = x % 4 == 0 ? 0xffffffffu : 0x00ffffffu;
        red[x] = x % 4 == 0 ? 0xffffffffu : 0xff000000u;
        shifted[x] = x % 4 == 2 ? 0xffffffffu : 0x00ffffffu;
    }
    const uint32_t grayValue = 0x40ffffff;
    const Ref<SampledTexture> alpha = draw.CreateTexture(side * 4, 1, rgba.data());
    const Ref<SampledTexture> opacity = draw.CreateTexture(side * 4, 1, red.data());
    const Ref<SampledTexture> otherWire = draw.CreateTexture(side * 4, 1, shifted.data());
    const Ref<SampledTexture> gray = draw.CreateTexture(1, 1, &grayValue);
    std::ofstream result(out / "subpixel-materials.csv");
    result << "case,samples_per_pixel,area,reference\n";

    auto check = [&](const char* name, const Ref<SampledTexture>& base, const Ref<SampledTexture>& holes,
                     float tint, float cutoff, float expected, bool twoLayers = false) {
        SurfaceMaterial material;
        material.AlphaCutoff = cutoff;
        material.Opacity = holes;
        draw.DrawSurfaces([&](CommandList& list) {
            if (twoLayers)
                list.DrawIndexed(quad, otherWire, glm::mat4(1), glm::translate(glm::mat4(1), {0,0,.2f}), glm::vec4(1), 8, material);
            list.DrawIndexed(quad, base, glm::mat4(1), glm::mat4(1), glm::vec4(1,1,1,tint), 7, material);
        });
        Image values(side * side);
        device.Readback(draw.GetAlbedo(), values.data(), values.size() * sizeof(glm::vec4));
        int covered = 0;
        for (const glm::vec4& pixel : values) if (pixel.a != 0) ++covered;
        float area = float(covered) / values.size();
        result << name << ",4," << area << ',' << expected << '\n';
        if (!twoLayers) Check(area == expected, name);
    };
    check("base-alpha", alpha, {}, 1.f, .5f, .25f);
    check("opacity-red", {}, opacity, 1.f, .5f, .25f);
    check("tinted-half-cutoff", alpha, {}, .5f, .25f, .25f);
    check("cutoff-three-quarters", alpha, {}, 1.f, .75f, 0.f);
    check("uniform-gray", gray, {}, 1.f, .5f, 0.f);
    check("disjoint-alpha-and-red", otherWire, opacity, 1.f, .5f, 0.f);
    check("opaque", {}, {}, 1.f, -1.f, 1.f);
    // 两张网各占四分之一，原图上的铁丝错开两列。这里只记录实际面积，保留这个重叠网片的反例。
    check("two-shifted-layers", alpha, {}, 1.f, .5f, .5f, true);
}

static void Run(VulkanContext& context,const fs::path& out,RenderDocCapture& capture)
{
    VulkanDraw draw(context,VulkanDraw::Output::LightingSurfaces);
    draw.Resize(Width,Height);
    VulkanComputeDevice device(context);
    constexpr int size=256;
    std::vector<uint32_t> bytes(size*size);
    for (int y=0; y<size; ++y)
        for (int x=0; x<size; ++x)
            bytes[y*size+x] = (x+y)%2 ? 0xffffffffu : 0xff000000u;
    auto checkers=draw.CreateTexture(size,size,bytes.data());

    // 黑白格沿地面重复。一块砖长一单位、含 256 个纹素；镜头只沿 X 平移。
    std::vector<MeshVertex> vertices;
    for (glm::vec2 xz : {glm::vec2(-8,1),glm::vec2(8,1),glm::vec2(8,-15),glm::vec2(-8,-15)})
        vertices.push_back({{xz.x,0,xz.y},{0,1,0},xz,{1,0,0}});
    auto ground=draw.CreateMesh(vertices,{0,1,2,2,3,0});
    const auto projection=glm::perspective(glm::radians(50.f),float(Width)/Height,.05f,50.f);
    SurfaceMaterial material;
    material.Metallic = material.Roughness = 1.f;
    material.MetallicRoughnessMap = checkers; // 同一份 RGBA 源同时作颜色与线性标量图。
    auto render=[&](float shift,bool record) {
        auto eye=glm::vec3(shift,1.6f,2.f);
        auto view=glm::lookAt(eye,glm::vec3(shift,0,-3),glm::vec3(0,1,0));
        if(record) capture.Begin();
        draw.DrawSurfaces([&](CommandList& commands) {
            commands.DrawIndexed(ground,checkers,projection*view,glm::mat4(1),glm::vec4(1),7,material);
        });
        if(record) capture.End();
        Image image(Width*Height);
        device.Readback(draw.GetAlbedo(),image.data(),image.size()*sizeof(image[0]));
        return image;
    };
    auto a=render(0,false), b=render(.007f,true);
    Picture(out/"frame-a.png",a); Picture(out/"frame-b.png",b);

    int count=0;
    double difference=0,error=0,mean=0;
    for(int y=43;y<86;++y)
        for(int x=32;x<Width-32;++x)
        {
            size_t i=y*Width+x;
            if(a[i].a==0 || b[i].a==0) continue;
            difference+=std::abs(a[i].r-b[i].r);
            error+=std::abs(b[i].r-.5f);
            mean+=b[i].r;
            ++count;
        }
    Check(count>5000,"Ground ROI not covered");
    difference/=count;error/=count;mean/=count;
    glm::vec4 landmark(0,0,-3,1);
    auto projectionX=[&](float shift) {
        auto view=glm::lookAt(glm::vec3(shift,1.6f,2.f),glm::vec3(shift,0,-3),glm::vec3(0,1,0));
        glm::vec4 clip=projection*view*landmark;
        return (clip.x/clip.w*.5f+.5f)*Width;
    };
    std::ofstream csv(out/"measurements.csv");
    csv<<"pixels,landmark_motion_pixels,mean_linear_R,mean_absolute_frame_difference,mean_absolute_error_from_half\n";
    csv<<count<<','<<std::abs(projectionX(.007f)-projectionX(0))<<','<<mean<<','<<difference<<','<<error<<'\n';
    std::cout<<"Pixels "<<count<<", mean "<<mean<<", frame difference "<<difference<<", error from 0.5 "<<error<<'\n';
    Check(difference<.002 && error<.01,"Minified texture shimmers or darkens");

    // 同一张图的绿色若是底色，64/192 要转成线性光再平均；若是粗糙度，它们就是两个数。
    for (int y=0; y<size; ++y)
        for (int x=0; x<size; ++x)
            bytes[y*size+x] = (x+y)%2 ? 0xffc0c0c0u : 0xff404040u;
    auto shared = draw.CreateTexture(size,size,bytes.data());
    material.MetallicRoughnessMap = shared;
    auto vp=projection*glm::lookAt(glm::vec3(0,1.6f,2.f),glm::vec3(0,0,-3.f),glm::vec3(0,1,0));
    draw.DrawSurfaces([&](CommandList& cmd) {
        cmd.DrawIndexed(ground,{},vp,glm::mat4(1),glm::vec4(1),7,material);
    });
    draw.DrawSurfaces([&](CommandList& cmd) {
        cmd.DrawIndexed(ground,shared,vp,glm::mat4(1),glm::vec4(1),7,material);
    });
    Image values(Width*Height);
    device.Readback(draw.GetAlbedo(),values.data(),values.size()*sizeof(values[0]));
    auto center=values[(Height/2)*Width+Width/2];
    uint32_t packed=static_cast<uint32_t>(center.a)-1u;
    float metallic=(packed>>12)/4095.f, roughness=(packed&4095u)/4095.f;
    auto decode=[](float byte) { return std::pow((byte/255.f+.055f)/1.055f,2.4f); };
    float ideal=.5f*(decode(64)+decode(192));
    Check(std::abs(center.g-ideal)<.006f && std::abs(roughness-128.f/255.f)<.005f &&
          std::abs(metallic-roughness)<.0003f,"Same image uses the wrong material encoding");
    std::ofstream uses(out/"roles.csv");
    uses<<"linear_base,ideal_linear_base,roughness,metallic,ideal_linear_data\n"
        <<center.g<<','<<ideal<<','<<roughness<<','<<metallic<<','<<128.f/255.f<<'\n';

    auto allocation=[&](VkImage image) {
        VkMemoryRequirements memory;
        vkGetImageMemoryRequirements(context.GetDevice(),image,&memory);
        return memory.size;
    };
    auto base=Vulkan::CreateImage(context,{size,size},VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
    auto& sampled=static_cast<const Vulkan::Texture&>(*shared);
    std::ofstream memory(out/"allocation.csv");
    memory<<"width,height,levels,old_single_level_bytes,color_chain_bytes,linear_chain_bytes\n"
          <<size<<','<<size<<",9,"<<allocation(base->Handle)<<','<<allocation(sampled.Color.Pixels->Handle)
          <<','<<allocation(sampled.Data.Pixels->Handle)<<'\n';

    // 左半边透明度 127，右半边 128。阈值 0.5 必须同时决定能不能看到、能不能点中。
    std::vector<MeshVertex> panel{
        {{-1,-1,0},{0,0,1},{0,0},{1,0,0}},{{1,-1,0},{0,0,1},{1,0},{1,0,0}},
        {{1,1,0},{0,0,1},{1,1},{1,0,0}},{{-1,1,0},{0,0,1},{0,1},{1,0,0}}};
    auto sheet=draw.CreateMesh(panel,{0,1,2,2,3,0});
    VulkanDraw picking(context);
    picking.Resize(Width,Height);
    std::ofstream masks(out/"mask.csv");
    masks<<"source,visibility_left,visibility_right,entity_left,entity_right\n";
    for (bool alpha : {true,false})
    {
        std::vector<uint32_t> mask(1024*8);
        for (int y=0;y<8;++y)
            for (int x=0;x<1024;++x)
            {
                uint32_t opacity=x<512 ? 127u : 128u;
                mask[y*1024+x]=alpha ? (opacity<<24)|0x00ffffffu : 0xff000000u|opacity;
            }
        auto map=draw.CreateTexture(1024,8,mask.data());
        SurfaceMaterial cutout;
        cutout.AlphaCutoff=.5f;
        if (!alpha) cutout.Opacity=map;
        auto paint=[&](CommandList& cmd) {
            cmd.DrawIndexed(sheet,alpha ? map : nullptr,glm::mat4(1),glm::mat4(1),glm::vec4(1),7,cutout);
        };
        draw.DrawSurfaces(paint);
        device.Readback(draw.GetAlbedo(),values.data(),values.size()*sizeof(values[0]));
        Check(context.BeginFrame(),"Cannot start picking frame");
        picking.BeginScene(); paint(picking); picking.EndScene(); context.SwapBuffers();
        bool left=values[Height/2*Width+Width/4].a>0, right=values[Height/2*Width+3*Width/4].a>0;
        int leftID=picking.ReadPixel(Width/4,Height/2).EntityID, rightID=picking.ReadPixel(3*Width/4,Height/2).EntityID;
        masks<<(alpha?"base_alpha":"opacity_red")<<','<<left<<','<<right<<','<<leftID<<','<<rightID<<'\n';
        Check(!left&&right&&leftID==-1&&rightID==7,"Alpha mask differs between picking and lighting");
    }
    CheckSubpixelMasks(context, out);
}

int main(int argc,char** argv)
{
    fs::path out=argc>1 ? argv[1] : "tmp/texture-minification";
    fs::create_directories(out);
    RenderDocCapture capture(argc>2 ? argv[2] : nullptr,out/"Floor","Minified material checkerboard",
        "256x144 perspective floor. Camera moved 0.007 world units right; no TAA, no lights, fixed linear G-buffer output.");
    Log::Init();
    if (!glfwInit()) return 1;
    int result=0;
    try
    {
        glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        std::unique_ptr<GLFWwindow,decltype(&glfwDestroyWindow)> window(glfwCreateWindow(Width,Height,"Floor",nullptr,nullptr),glfwDestroyWindow);
        VulkanContext context(window.get());context.Init();
        Run(context,out,capture);
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n';result=1; }
    glfwTerminate();
    return result;
}
