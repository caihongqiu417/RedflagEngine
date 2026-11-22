#include "rfpch.h"
#include "../RedflagForge/src/LightingPreview.h"
#include "../Redflag/vendor/stb_image/stb_image_write.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Core/Application.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Renderer/SceneRenderer.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Scene/SceneSerializer.h"
#include "RenderDocCapture.h"
#include <glm/gtc/matrix_transform.hpp>

using namespace Redflag;
constexpr uint32_t Width = 768, Height = 432;
const std::filesystem::path Output = "../output/validation/material-height";
static void Check(bool pass,const char* what) { if (!pass) throw std::runtime_error(what); }

struct Frame
{
    std::vector<glm::vec4> Color, NormalDepth;
    std::vector<int> IDs;
};

struct Difference
{
    size_t Color=0, Normals=0, Depth=0, IDs=0;
};

static Difference Compare(const Frame& a,const Frame& b)
{
    Difference result;
    for (size_t i=0;i<a.Color.size();++i)
    {
        result.Color += glm::distance(glm::vec3(a.Color[i]),glm::vec3(b.Color[i]))>.002f;
        result.Normals += glm::distance(glm::vec3(a.NormalDepth[i]),glm::vec3(b.NormalDepth[i]))>.01f;
        result.Depth += std::abs(a.NormalDepth[i].w-b.NormalDepth[i].w)>1e-5f;
        result.IDs += a.IDs[i]!=b.IDs[i];
    }
    return result;
}

int main(int argc, char** argv) try
{
    const char* names[]={"off","bump","parallax","displacement"};
    int captured = argc>2 ? std::stoi(argv[2]) : 0;
    Check(captured>=0 && captured<4,"Expected mode 0 through 3");
    RenderDocCapture capture(argc>3 ? argv[3] : nullptr,"../output/captures/local/height/"+std::string(names[captured]),
        "Gravel material: same view and geometry","768x432 G-buffer. Capture of one indexed surface draw, GPU time via EventGPUDuration.");

    std::filesystem::create_directories(Output);
    Log::Init();
    RendererAPI::SetAPI(RendererAPI::API::Vulkan);
    Application app("Height material check",false);
    auto& context=static_cast<VulkanContext&>(app.GetWindow().GetGraphicsContext());
    VulkanDraw picking(context), surfaces(context,VulkanDraw::Output::LightingSurfaces);
    VulkanComputeDevice compute(context);
    picking.Resize(Width,Height);
    surfaces.Resize(Width,Height);
    RenderDevice::SetCurrent(&picking);
    const std::string path="assets/scenes/Relief-Study.redflag";
    Ref<Scene> scene=CreateRef<Scene>();
    scene->OnViewportResize(Width,Height);
    Check(SceneSerializer(scene).Deserialize(path),"Read material study");
    Ref<Material> mat=scene->GetEntityByUUID(102).GetComponent<ModelRendererComponent>().OverrideMaterials.at(0);
    Check(mat->HeightMap && mat->HeightMap->IsLoaded() && mat->HeightMode==HeightMapping::Parallax && !mat->NormalMap,
          "Load authored height separately from RGB normals");
    Entity camera=scene->GetPrimaryCameraEntity();
    const glm::mat4 projection=camera.GetComponent<CameraComponent>().Camera.GetProjection();
    const glm::mat4 view=glm::inverse(camera.GetComponent<TransformComponent>().GetTransform());
    glm::mat4 vp=projection*view;
    const glm::vec3 eye=camera.GetComponent<TransformComponent>().Translation;

    auto draw=[&](CommandList& commands) { SceneRenderer::DrawGeometry(*scene,commands,vp); };
    auto frame=[&]() {
        Frame image;
        image.Color.resize(Width*Height);
        image.NormalDepth.resize(Width*Height);
        image.IDs.resize(Width*Height);
        std::vector<glm::u8vec4> pickColor(Width*Height);
        surfaces.DrawSurfaces(draw);
        compute.Readback(surfaces.GetAlbedo(),image.Color.data(),image.Color.size()*sizeof(glm::vec4));
        compute.Readback(surfaces.GetNormalDepth(),image.NormalDepth.data(),image.NormalDepth.size()*sizeof(glm::vec4));
        Check(context.BeginFrame(),"Begin picking frame");
        picking.BeginScene();
        draw(picking);
        picking.EndScene();
        context.SwapBuffers();
        picking.ReadRegion(0,0,Width,Height,pickColor.data(),image.IDs.data());
        return image;
    };
    if (argc>3)
    {
        mat->HeightMode=HeightMapping(captured);
        surfaces.DrawSurfaces(draw);
        capture.Begin();
        surfaces.DrawSurfaces(draw);
        capture.End();
    }
    else
    {
        LightingPreview lit(context,picking.GetSampler());
        lit.Load(path);
        std::ofstream report(Output/"metrics.csv");
        report << "mode,changed_albedo,changed_normal,changed_depth,changed_picking\n";
        Frame reference;
        for (int index=0;index<4; ++index)
        {
            mat->HeightMode=HeightMapping(index);
            Frame image=frame();
            if (!index) reference=image;
            Difference diff=Compare(reference,image);
            report << names[index] << ',' << diff.Color << ',' << diff.Normals << ',' << diff.Depth << ',' << diff.IDs << '\n';
            std::cout << names[index] << ": " << diff.Color << ' ' << diff.Normals << ' ' << diff.Depth << ' ' << diff.IDs << std::endl;
            if (index==1) Check(!diff.Color && diff.Normals>500 && !diff.Depth && !diff.IDs,"Bump changes only surface normal");
            if (index==2) Check(diff.Color>500 && diff.Normals>500 && !diff.Depth && !diff.IDs,"Parallax shifts UV but preserves geometry");
            if (index==3) Check(diff.Depth>500 && diff.IDs>20,"Displaced triangles change depth and silhouette");
            Check(lit.Render(*scene,projection,view,eye,Width,Height,false,false)!=VK_NULL_HANDLE,"Render real game material");
            auto pixels=lit.ReadDisplayPixels();
            Check(stbi_write_png((Output/(std::string(names[index])+".png")).u8string().c_str(),Width,Height,4,pixels.data(),Width*4)!=0,"Save real Lit output");
        }

        mat->HeightScale=0;
        Difference off=Compare(reference,frame());
        Check(!off.Color && !off.Normals && !off.Depth && !off.IDs,"Zero height exactly restores original material");
        mat->HeightScale=.055f;
        mat->HeightMode=HeightMapping::Parallax;
        vp=glm::ortho(-1.4f,1.4f,-.85f,.85f,.1f,8.f)*glm::lookAt(glm::vec3(0,3,0),glm::vec3(0),glm::vec3(0,0,-1));
        Frame front=frame();
        mat->HeightMode=HeightMapping::Off;
        Check(!Compare(front,frame()).Color,"No UV offset looking straight down with orthographic projection");

        // 置换参考面设为黑时，一张全黑高度图应使所有顶点停在原位。
        vp=projection*view;
        Ref<Texture2D> gravel=mat->HeightMap;
        mat->HeightMap=Texture2D::Create(1,1);
        uint8_t black[]={0,0,0,255};
        mat->HeightMap->SetData(black,sizeof(black));
        mat->HeightMode=HeightMapping::Displacement;
        mat->HeightReference=0.f;
        Difference neutral=Compare(reference,frame());
        Check(!neutral.Depth && !neutral.IDs,"The selected reference value keeps the original geometry");
        mat->HeightReference=1.f;
        Check(Compare(reference,frame()).Depth>500,"Changing the reference moves the vertices");
        mat->HeightMap=gravel;

        // 计时范围到场景命令写完为止。DrawSurfaces 在回调外提交队列，等待时间不计入 CPU 录制。
        vp=projection*view;
        std::ofstream timing(Output/"record-cpu-us.csv");
        timing << "mode,microseconds\n";
        for (int mode : {0,1,2,3,3,2,1,0})
        {
            mat->HeightMode=HeightMapping(mode);
            surfaces.DrawSurfaces(draw);
            for (int i=0;i<16;++i)
                surfaces.DrawSurfaces([&](CommandList& commands) {
                    const auto begin=std::chrono::steady_clock::now();
                    draw(commands);
                    const auto end=std::chrono::steady_clock::now();
                    timing << names[mode] << ',' << std::chrono::duration<double,std::micro>(end-begin).count() << '\n';
                });
        }

        // OBJ 的 map_Bump 是一幅灰度高度图。它在导入时不能占据 RGB 法线图的位置。
        const auto sample=std::filesystem::path("../tmp/height-obj-import");
        std::filesystem::create_directories(sample);
        std::ofstream(sample/"tile.obj") << "mtllib surface.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nusemtl tile\nf 1/1 2/2 3/3\n";
        std::ofstream(sample/"surface.mtl") << "newmtl tile\nmap_Bump ridges.png\n";
        const uint8_t gradient[]={0,128,128,255};
        stbi_write_png((sample/"ridges.png").string().c_str(),2,2,1,gradient,2);
        auto obj=Model::Load((sample/"tile.obj").string());
        Check(obj && obj->GetSubMeshes()[0].Mat->HeightMode==HeightMapping::Bump &&
              obj->GetSubMeshes()[0].Mat->HeightMap && !obj->GetSubMeshes()[0].Mat->NormalMap,"Import OBJ map_Bump as height");

        const auto normalSample=sample/"old-material";
        std::filesystem::create_directories(normalSample);
        std::filesystem::copy_file(sample/"tile.obj",normalSample/"tile.obj",std::filesystem::copy_options::overwrite_existing);
        std::ofstream(normalSample/"surface.mtl") << "newmtl tile\nmap_Bump tile_normal.png\n";
        const uint8_t normal[]={128,128,255};
        stbi_write_png((normalSample/"tile_normal.png").string().c_str(),1,1,3,normal,3);
        const Ref<Model> old=Model::Load((normalSample/"tile.obj").string());
        Check(old && old->GetSubMeshes()[0].Mat->NormalMap && !old->GetSubMeshes()[0].Mat->HeightMap,
              "Legacy RGB normal map keeps its original meaning");

        const auto clone=mat->Clone();
        Check(clone->HeightMap==mat->HeightMap && clone->HeightScale==mat->HeightScale,"Cloning retains the height map and scale");
        mat->HeightMode=HeightMapping::Displacement;
        mat->HeightReference=.522f;
        SceneSerializer(scene).Serialize("../tmp/height-saved.redflag");
        auto opened=CreateRef<Scene>(); opened->OnViewportResize(Width,Height);
        Check(SceneSerializer(opened).Deserialize("../tmp/height-saved.redflag"),"Read saved material");
        const auto saved=opened->GetEntityByUUID(102).GetComponent<ModelRendererComponent>().OverrideMaterials.at(0);
        Check(saved->HeightMap && saved->HeightMap->IsLoaded() && saved->HeightMode==HeightMapping::Displacement &&
              saved->HeightScale==mat->HeightScale && saved->HeightReference==mat->HeightReference,
              "Saved height, mode, scale and neutral surface");
    }
    context.WaitIdle();
    scene.reset();
    AssetManager::ClearAll();
    RenderDevice::SetCurrent(nullptr);
    std::cout << "PASS material heights\n";
    return 0;
}
catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
