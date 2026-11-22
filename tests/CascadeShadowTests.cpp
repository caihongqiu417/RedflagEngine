#include "rfpch.h"
#include "Platform/Vulkan/VulkanCascadeMaps.h"
#include "Platform/Vulkan/VulkanComputeDevice.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Redflag/Renderer/Mesh.h"
#include "Redflag/Renderer/ShadowCascades.h"
#include "RenderDocCapture.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

using namespace Redflag;
namespace fs=std::filesystem;

static void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void CheckLayers()
{
    glm::mat4 view=glm::lookAt(glm::vec3(40,2,10),glm::vec3(40,2,9),glm::vec3(0,1,0));
    for (bool ortho : {false,true})
    {
        glm::mat4 projection=ortho ? glm::ortho(-4.f,4.f,-3.f,3.f,-1.f,80.f)
            : glm::perspective(glm::radians(60.f),4.f/3.f,.1f,80.f);
        auto layers=BuildShadowCascades(projection,view,{0,-1,0},{-100,0,-100},{150,45,100},3,40.f,256);
        Check(layers.size()==3 && std::abs(layers.back().FarDepth-40.f)<.001f,"wrong shadow distance");
        if (!ortho) Check(layers.front().TexelSize < layers.back().TexelSize*.5f,"near layer must have more detail");
        float begin=ortho ? -1.f : .1f;
        for (const auto& layer : layers)
        {
            for (float z : {begin,layer.FarDepth})
                for (float x : {-1.f,1.f})
                    for (float y : {-1.f,1.f})
                    {
                        glm::vec4 clip=projection*glm::vec4(0,0,-z,1);
                        glm::vec4 w=glm::inverse(projection*view)*glm::vec4(x,y,clip.z/clip.w,1);
                        glm::vec4 p=layer.ViewProjection*(w/w.w);
                        Check(glm::all(glm::lessThanEqual(glm::abs(glm::vec3(p)),glm::vec3(1.001f))),"clipped a camera slice corner");
                    }
            begin=layer.FarDepth;
        }
    }
}

static void CheckShadows(VulkanContext& context, VulkanComputeDevice& device, const fs::path& out,
    RenderDocCapture& capture, bool profile)
{
    // 近、中、远三块水平挡板各位于一层。中间一块离地八米，镜头看不见它，地面仍应收到影子。
    const glm::vec3 eye(0,1,2);
    const glm::mat4 view=glm::lookAt(eye,eye+glm::vec3(0,0,-1),{0,1,0});
    const glm::mat4 projection=glm::perspective(glm::radians(55.f),16.f/9.f,.1f,50.f);
    const glm::vec3 low(-2,0,-24), high(2,8,0);
    std::vector<GILight> lights{GILight::Directional({0,-1,0},{1,1,1},true)};
    VulkanDraw meshDevice(context);
    auto quad=meshDevice.CreateMesh({
        {{-1,0,-1},{0,1,0},{0,0},{1,0,0}},{{-1,0,1},{0,1,0},{0,1},{1,0,0}},
        {{1,0,1},{0,1,0},{1,1},{1,0,0}},{{1,0,-1},{0,1,0},{1,0},{1,0,0}}
    },{0,1,2,2,3,0});
    VulkanCascadeMaps shadows(context,device);
    std::vector<glm::vec4> points;
    const glm::vec2 boards[]={{0,.08f},{-6.f,8.f},{-23.f,1.4f}};
    for (float z : {-7.f,-5.f})
    {
        glm::vec4 clip=projection*view*glm::vec4(0,8,z,1);
        Check(clip.y>clip.w,"elevated caster must be above the camera view");
    }
    constexpr unsigned samples=257;
    for (glm::vec2 board : boards)
        for (unsigned i=0;i<samples;++i) points.emplace_back(2.f*i/(samples-1),0,board.x,1);
    std::ifstream file("bin-int/Tests/ShadowReceiver.spv",std::ios::binary|std::ios::ate);
    Check(bool(file),"compile ShadowReceiver.comp first");
    size_t bytes=size_t(file.tellg());
    std::vector<uint32_t> words(bytes/4);
    file.seekg(0); file.read(reinterpret_cast<char*>(words.data()),bytes);
    auto probe=device.CreatePipeline(words.data(),bytes,std::vector<ComputeBinding>(3,ComputeBinding::Buffer),sizeof(glm::vec4));
    auto positions=device.CreateBuffer(points.size()*sizeof(points[0]),points.data());
    auto result=device.CreateBuffer(points.size()*sizeof(float));
    std::vector<float> values(points.size());
    fs::create_directories(out);
    std::ofstream profiles(out/"profiles.csv");
    profiles<<"angle_deg,board,x_m,visibility\n";
    for (float angle : {0.f,4.f})
    {
        if (angle>0) capture.Begin();
        unsigned draws=0;
        auto maps=shadows.Draw(projection,view,lights,low,high,3,512,32.f,angle,
            [&](CommandList& commands,const glm::mat4& vp) {
                SurfaceMaterial material;
                material.DoubleSided=true;
                for (glm::vec2 board : boards)
                {
                    const glm::mat4 model=glm::translate(glm::mat4(1),glm::vec3(0,board.y,board.x));
                    commands.DrawIndexed(quad,nullptr,vp,model,glm::vec4(1),1,material);
                    ++draws;
                }
            });
        const glm::vec4 toLight(0,1,0,0);
        device.Dispatch(probe,{maps,positions,result},&toLight,uint32_t((points.size()+63)/64));
        if (angle>0) capture.End();
        device.Readback(result,values.data(),values.size()*sizeof(float));
        Check(draws==9,"each of three cascades must render the three boards");
        int transition[3]{};
        for (unsigned b=0;b<3;++b)
        {
            const float* line=values.data()+samples*b;
            Check(line[0]==0 && line[samples-1]==1,"missing shadow or wrong receiver on a cascade");
            for (unsigned i=0;i<samples;++i)
            {
                Check(std::isfinite(line[i])&&line[i]>=0&&line[i]<=1,"invalid shadow visibility");
                transition[b]+=(line[i]>.05f && line[i]<.95f);
                profiles<<angle<<','<<b<<','<<points[samples*b+i].x<<','<<line[i]<<'\n';
            }
        }
        std::cout<<"angle "<<angle<<" degrees: transition samples "<<transition[0]<<' '<<transition[1]<<' '<<transition[2]<<'\n';
        if (angle>0) Check(transition[1]>transition[0]+10,"the higher blocker must cast a wider penumbra");
    }

    // 将同一平地同时作为投影物与接收面。太阳斜照时，远处一格地面的深度应按地面坡度换算到当前格。
    const glm::vec3 direction = glm::normalize(glm::vec3(.65f,-1.f,.32f));
    lights[0] = GILight::Directional(direction,{1,1,1},true);
    const glm::vec4 toLight(-direction,0);
    const glm::mat4 floor = glm::scale(glm::mat4(1),glm::vec3(30,1,30));
    for (float angle : {0.f,4.f})
    {
        auto maps = shadows.Draw(projection,view,lights,{-30,0,-30},{30,0,30},3,512,32.f,angle,
            [&](CommandList& commands, const glm::mat4& vp) {
                SurfaceMaterial material;
                material.DoubleSided=true;
                commands.DrawIndexed(quad,nullptr,vp,floor,glm::vec4(1),1,material);
            });
        device.Dispatch(probe,{maps,positions,result},&toLight,uint32_t((points.size()+63)/64));
        device.Readback(result,values.data(),values.size()*sizeof(float));
        const float darkest = *std::min_element(values.begin(),values.end());
        std::cout<<"Unobstructed ground, sun radius "<<angle<<" degrees: minimum visibility "<<darkest<<'\n';
        Check(darkest > .999f, "a slanting light produced false shadows on the floor");
    }

    if (!profile) return;
    std::ofstream cpu(out/"cpu.csv");
    cpu << "layers,iteration,record_mesh_ms,draw_and_wait_ms\n";
    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::duration<double,std::milli>;
    for (unsigned count : {1u,3u,3u,1u})
    {
        for (int i=-3; i<15; ++i)
        {
            double recording=0;
            const auto begin=Clock::now();
            shadows.Draw(projection,view,lights,low,high,count,512,32.f,4.f,
                [&](CommandList& commands,const glm::mat4& vp) {
                    const auto record=Clock::now();
                    SurfaceMaterial material;
                    material.DoubleSided=true;
                    for (glm::vec2 board : boards) {
                        const glm::mat4 model=glm::translate(glm::mat4(1),glm::vec3(0,board.y,board.x));
                        commands.DrawIndexed(quad,nullptr,vp,model,glm::vec4(1),1,material);
                    }
                    recording+=Milliseconds(Clock::now()-record).count();
                });
            const double elapsed=Milliseconds(Clock::now()-begin).count();
            if (i>=0) cpu<<count<<','<<i<<','<<recording<<','<<elapsed<<'\n';
        }
    }
}

int main(int argc,char** argv)
{
    if (argc!=2 && argc!=3) return 2;
    const bool profile=argc==3 && std::string(argv[2])=="--profile";
    const bool record=argc==3 && !profile;
    RenderDocCapture capture(record?argv[2]:nullptr,"output/profiling/cascades/Shadow",
        "Cascaded sun shadows","Three camera-distance slices. Ground receiver, elevated blocker, soft sun disk.");
    Log::Init();
    if (!glfwInit()) return 1;
    int result=0;
    try
    {
        CheckLayers();
        glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        std::unique_ptr<GLFWwindow,decltype(&glfwDestroyWindow)> window(glfwCreateWindow(96,64,"CSM",nullptr,nullptr),glfwDestroyWindow);
        Check(bool(window),"cannot create Vulkan test window");
        VulkanContext context(window.get()); context.Init();
        if (record)
        {
            Check(context.BeginFrame(), "cannot acquire a RenderDoc capture window");
            context.BeginWindowPass();
            context.EndWindowPass();
            context.SwapBuffers();
        }
        VulkanComputeDevice device(context);
        CheckShadows(context,device,argv[1],capture,profile);
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; result=1; }
    glfwTerminate();
    return result;
}
