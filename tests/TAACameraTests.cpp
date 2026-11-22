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
    // 16 个位置均落在小格中心。像素从左上角起算，减去 (0.5, 0.5) 后得到相对中心的偏移。
    const glm::vec2 expected[] = {{3.f/32,3.f/32}, {-7.f/32,-11.f/32}, {-3.f/32,13.f/32}};
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
    // Vulkan 规范的 16 样点表，这里按从上到下的行号查列号。顺序可以重排，集合与中心位置必须一致。
    constexpr int columnForRow[] = {1,8,4,11,15,7,3,12,0,9,5,13,2,10,6,14};
    uint32_t rows[2]{};
    bool visited[16][16]{};
    glm::vec2 center(0);
    const glm::vec4 point{0,0,-4,1};
    for (uint32_t frame=0; frame<32; ++frame)
    {
        glm::mat4 jitter = ScreenEffects::JitterProjection(projection,frame,Width,Height);
        glm::vec2 shift = Pixel(jitter,point)-Pixel(projection,point);
        glm::ivec2 cell = glm::ivec2(glm::floor((shift+.5f)*16.f));
        if (cell.x<0 || cell.x>=16 || cell.y<0 || cell.y>=16 || visited[cell.y][cell.x]) return 1.f;
        glm::vec2 expectedShift = (glm::vec2(cell)+.5f)/16.f-.5f;
        error = std::max(error,glm::length(shift-expectedShift));
        visited[cell.y][cell.x] = true;
        if (frame>=16) cell = {cell.y,15-cell.x}; // 把第二组转回规范表的方向。
        if (columnForRow[cell.y]!=cell.x) return 1.f;
        rows[frame/16] |= 1u << cell.y;
        center += shift/32.f;
    }
    if (rows[0]!=0xffffu || rows[1]!=0xffffu) return 1.f;
    error = std::max(error,glm::length(center));
    return error;
}

// 每个像素内均匀取 64×64 个位置，用两根矩形细线的实际长宽求覆盖面积，参考值与 TAA 的采样顺序无关。
static float WireCoverage(int x, int y, float eyeY)
{
    static const glm::vec2 directions[] = {{std::cos(.21f),std::sin(.21f)}, {std::cos(-.13f),std::sin(-.13f)}};
    const glm::vec2 center{x+.5f-Width/2, Height/2+eyeY-y-.5f};
    auto covered = [&](glm::vec2 point,float margin) {
        for (glm::vec2 u : directions) {
            float along = glm::dot(point,u), across = point.y*u.x-point.x*u.y;
            if (std::abs(along)<=58.f+margin && std::abs(across)<=.16f+margin) return true;
        }
        return false;
    };
    if (!covered(center,.71f)) return 0.f; // 像素中心到最远的角约为 0.707 像素。
    int hits = 0;
    for (int j=0; j<64; ++j)
        for (int i=0; i<64; ++i) {
            glm::vec2 offset((i+.5f)/64.f-.5f,(j+.5f)/64.f-.5f);
            hits += covered(center+offset,0.f);
        }
    return hits/4096.f;
}

static void WriteMotionError(std::ofstream& csv, int step, const Image& frame, const Image& previous,
                             const std::vector<float>& reference, const std::vector<float>& oldReference)
{
    int edges = 0;
    double imageError = 0., changeError = 0., area = 0., measured = 0.;
    float trail = 0.f;
    for (size_t i=0; i<reference.size(); ++i) {
        area += reference[i];
        measured += frame[i].r;
        if (reference[i]==0.f) trail = std::max(trail,frame[i].r);
        if (reference[i]==0.f && oldReference[i]==0.f) continue;
        ++edges;
        imageError += std::abs(frame[i].r-reference[i]);
        changeError += std::abs((frame[i].r-previous[i].r) - (reference[i]-oldReference[i]));
    }
    csv << step << ',' << step/8.f << ',' << edges << ',' << imageError/edges << ','
        << changeError/edges << ',' << trail << ',' << measured/area << '\n';
}

static bool CheckWires(VulkanComputeDevice& device, VulkanDraw& draw, const Ref<MeshGeometry>& quad,
                       GlobalIllumination& gi, const Ref<ComputePipeline>& pack,
                       const Ref<ComputeResource>& plain, const fs::path& out)
{
    const glm::mat4 projection = glm::ortho(-64.f,64.f,-32.f,32.f,.1f,32.f);
    const glm::mat4 wall = glm::scale(glm::translate(glm::mat4(1),{-32,0,-12}),{32,32,1});
    auto wire = [](float z, float angle) {
        glm::mat4 center = glm::translate(glm::mat4(1),{0,0,z});
        return glm::scale(glm::rotate(center,angle,{0,0,1}),{58.f,.16f,1.f});
    };
    const glm::mat4 first = wire(-7,.21f), second = wire(-8,-.13f);
    ScreenEffects effects(device,Width,Height);
    effects.GetSettings().HistoryWeight = .85f;
    Image raw(Width*Height), filtered(Width*Height);
    uint32_t frame = 0;
    auto raster = [&](const glm::mat4& vp, bool wires) {
        const glm::mat4 inv = glm::inverse(vp);
        draw.DrawSurfaces([&](CommandList& list) {
            list.DrawIndexed(quad,{},vp,wall,{0,0,.5f,1},1);
            if (wires) {
                list.DrawIndexed(quad,{},vp,first,glm::vec4(1),2);
                list.DrawIndexed(quad,{},vp,second,glm::vec4(1),3);
            }
        });
        device.Dispatch(pack,{draw.GetAlbedo(),draw.GetNormalDepth(),gi.GetSurfaces()},&inv,(Width+7)/8,(Height+7)/8);
        device.Readback(draw.GetAlbedo(),raw.data(),raw.size()*sizeof(glm::vec4));
    };
    auto render = [&](float eyeY, bool wires) {
        const glm::mat4 view = glm::translate(glm::mat4(1),{0,-eyeY,0});
        const glm::mat4 jitter = ScreenEffects::JitterProjection(projection,frame,Width,Height);
        raster(jitter*view,wires);
        device.Upload(plain,raw.data(),raw.size()*sizeof(glm::vec4));
        Ref<ComputeResource> result = effects.Render(ScreenEffect::TAA,gi,jitter,view,frame++,plain,&projection);
        device.Readback(result,filtered.data(),filtered.size()*sizeof(glm::vec4));
    };

    // 同样的两根实心细线在左半图挡住墙，在右半图挡住天空。均值来自实际画出的 32 张未混合图。
    std::ofstream csv(out/"wires.csv");
    csv << "camera,background,edge_pixels,capped_blend_mae,coverage_mae,max_error\n";
    std::ofstream motion(out/"wire-motion.csv");
    motion << "uncovered_pixels,max_old_wire_color\n";
    std::ofstream spatial(out/"wire-fixed-geometry.csv");
    spatial << "camera,background,edge_pixels,coverage_mae\n";
    bool passed = true;
    for (float eyeY : {0.f,3.375f})
    {
        Image moving;
        if (eyeY != 0.f) {
            render(eyeY,true);
            moving = filtered;
        }
        std::vector<float> coverage(raw.size()), oldBlend(raw.size());
        for (int sample=0; sample<32; ++sample)
        {
            render(eyeY,true);
            float oldWeight = std::min(.85f,sample/(sample+1.f));
            for (size_t i=0; i<raw.size(); ++i) {
                coverage[i] += raw[i].r / 32.f;
                oldBlend[i] = glm::mix(raw[i].r,oldBlend[i],oldWeight);
            }
        }
        for (int region=0; region<3; ++region)
        {
            int count = 0;
            float error = 0.f, oldError = 0.f, largest = 0.f;
            // 两根线在画面中心相交。另取交点两旁各 4 列，确认单根线上的结果也适用于这里。
            const int starts[] = {0, Width/2, Width/2-4}, ends[] = {Width/2, Width, Width/2+4};
            int geometricEdges = 0;
            double areaError = 0.f;
            for (int y=0; y<Height; ++y)
                for (int x=starts[region]; x<ends[region]; ++x)
                {
                    size_t i = size_t(y)*Width+x;
                    float area = WireCoverage(x,y,eyeY);
                    if (area>0.f && area<1.f) {
                        ++geometricEdges;
                        areaError += std::abs(filtered[i].r-area);
                    }
                    if (coverage[i] <= 0.f || coverage[i] >= 1.f) continue;
                    float difference = std::abs(filtered[i].r-coverage[i]);
                    error += difference;
                    oldError += std::abs(oldBlend[i]-coverage[i]);
                    largest = std::max(largest,difference);
                    ++count;
                }
            if (!count) return false;
            const char* names[] = {"wall","sky","crossing"};
            spatial << eyeY << ',' << names[region] << ',' << geometricEdges << ',' << areaError/geometricEdges << '\n';
            csv << eyeY << ',' << names[region] << ',' << count << ',' << oldError/count
                << ',' << error/count << ',' << largest << '\n';
            passed &= count>(region==2 ? 8 : 100) && largest<.0001f && oldError/count>.01f;
        }
        if (!moving.empty())
        {
            size_t uncovered = 0;
            float trail = 0.f;
            for (size_t i=0; i<moving.size(); ++i) {
                if (coverage[i] != 0.f) continue;
                ++uncovered;
                trail = std::max(trail,moving[i].r);
            }
            motion << uncovered << ',' << trail << '\n';
            passed &= uncovered>100 && trail<.0001f;
        }
        Save(out/(eyeY == 0.f ? "wire-fixed.png" : "wire-stopped.png"),filtered);
    }
    effects.ResetHistory();
    render(3.375f,false);
    for (const glm::vec4& color : filtered) passed &= color.r == 0.f;
    effects.GetSettings().HistoryWeight = 0.f;
    render(3.375f,true);
    for (size_t i=0; i<raw.size(); ++i) passed &= filtered[i].r == raw[i].r;

    // 每帧沿 Y 平移 1/8 个像素。参考图在该机位独立画 32 次，再对 32 个位置的光栅化结果取算术平均。
    effects.ResetHistory();
    effects.GetSettings().HistoryWeight = .85f;
    for (int sample=0; sample<32; ++sample) render(0.f,true);
    Image lastFrame = filtered;
    std::vector<float> lastCoverage(raw.size());
    for (size_t i=0; i<raw.size(); ++i) lastCoverage[i] = filtered[i].r;

    std::ofstream pan(out/"wire-pan.csv");
    pan << "frame,pan_pixels,edge_pixels,coverage_mae,change_error_mae,max_color_outside_wire,coverage_ratio\n";
    std::ofstream geometry(out/"wire-pan-geometry.csv");
    geometry << "frame,pan_pixels,edge_pixels,coverage_mae,change_error_mae,max_color_outside_wire,coverage_ratio\n";
    std::vector<float> lastArea(raw.size());
    for (size_t i=0; i<raw.size(); ++i) lastArea[i]=WireCoverage(int(i%Width),int(i/Width),0.f);
    for (int step=1; step<=16; ++step)
    {
        const float eyeY = step/8.f;
        render(eyeY,true);
        const glm::mat4 view = glm::translate(glm::mat4(1),{0,-eyeY,0});
        std::vector<float> coverage(raw.size());
        for (uint32_t phase=0; phase<32; ++phase) {
            raster(ScreenEffects::JitterProjection(projection,phase,Width,Height)*view,true);
            for (size_t i=0; i<raw.size(); ++i) coverage[i] += raw[i].r/32.f;
        }
        std::vector<float> area(raw.size());
        for (size_t i=0; i<area.size(); ++i) area[i] = WireCoverage(int(i%Width),int(i/Width),eyeY);
        WriteMotionError(pan,step,filtered,lastFrame,coverage,lastCoverage);
        WriteMotionError(geometry,step,filtered,lastFrame,area,lastArea);
        lastFrame = filtered;
        lastCoverage = std::move(coverage);
        lastArea = std::move(area);
    }
    return passed;
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
    return CheckWires(device,draw,quad,gi,pack,plain,out) && passed;
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
