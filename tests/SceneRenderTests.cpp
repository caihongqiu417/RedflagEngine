#include "rfpch.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Renderer/Framebuffer.h"
#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Renderer2D.h"
#include "Redflag/Renderer/Renderer3D.h"
#include "Redflag/Renderer/SceneRenderer.h"
#include "Redflag/Renderer/Shader.h"
#include "Redflag/Scene/Scene.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Scene/SceneSerializer.h"
#include "ScenePhysicsTests.h"
#include "../RedflagForge/src/EditorEnvironment.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

using namespace Redflag;
static int s_Failures = 0;
static void Check(bool condition, const char* name)
{
    std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
    s_Failures += !condition;
}

static void CheckSkySettings()
{
    const char* path = "../bin-int/Tests/sky-settings.redflag";
    Ref<Scene> scene = CreateRef<Scene>();
    scene->SetEnvironment(DefaultSky);
    scene->SetSkyVisible(false);
    SceneSerializer(scene).Serialize(path);

    Ref<Scene> saved = CreateRef<Scene>();
    Check(SceneSerializer(saved).Deserialize(path) && !saved->IsSkyVisible() &&
          saved->GetEnvironment() == std::filesystem::absolute(DefaultSky).lexically_normal(), "reopen the hidden sky and its lighting map");
    Ref<Scene> copy = Scene::Copy(scene);
    scene->SetSkyVisible(true);
    Check(!copy->IsSkyVisible() && copy->GetEnvironment() == DefaultSky, "the copied scene keeps its own sky setting");

    std::ofstream(path) << "Scene: Legacy\nEntities: []\n";
    Check(SceneSerializer(saved).Deserialize(path) && saved->IsSkyVisible() && saved->GetEnvironment().empty(),
          "legacy scene defaults to visible sky and retains its empty environment");
}

static void CheckDaylight()
{
    Scene scene;
    scene.SetEnvironment(DefaultSky);
    Entity cube = scene.CreateEntity("Metal");
    MeshRendererComponent& surface = cube.AddComponent<MeshRendererComponent>();
    surface.MeshData = Mesh::CreateCube();
    surface.Mat = Material::Create(glm::vec4(1));
    surface.Mat->Metallic = 1.f;
    surface.Mat->Roughness = .25f;

    FramebufferSpecification spec;
    spec.Width = 33; spec.Height = 17;
    spec.Attachments = {FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER,
                        FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH32F};
    Ref<Framebuffer> target = Framebuffer::Create(spec);
    SceneRenderer::SetMainSceneColorTarget(target->GetColorAttachmentRendererID(), 33, 17);
    EditorCamera camera(45, 33.f / 17.f, .1f, 100);
    camera.LookAt({0, 0, 4}, {0, 0, 0});

    auto draw = [&](bool editor) {
        target->Bind();
        glDepthMask(GL_TRUE);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        target->ClearAttachment(1, -1);
        if (editor) SceneRenderer::RenderScene(scene, camera);
        else SceneRenderer::RenderRuntime(scene, camera.GetProjection(), glm::inverse(camera.GetViewMatrix()));
    };
    auto color = [&](int x) {
        glm::vec3 rgb;
        glGetTextureSubImage(target->GetColorAttachmentRendererID(), 0, x, 8, 0, 1, 1, 1, GL_RGB, GL_FLOAT, sizeof(rgb), &rgb);
        return rgb;
    };

    // 左侧看天空，中心看同一个金属面。只切换环境照明时，只有金属的读数应变化。
    for (bool editor : {true, false})
    {
        SceneRenderer::GetIBLEnabled() = true;
        draw(editor);
        const glm::vec3 sky = color(1), metal = color(16);
        scene.SetSkyVisible(false);
        draw(editor);
        Check(color(1) == glm::vec3(0) && color(16) == metal, "hiding the sky keeps the metal reflection");
        scene.SetSkyVisible(true);
        SceneRenderer::GetIBLEnabled() = false;
        draw(editor);
        target->Bind();
        Check(target->ReadPixel(1, 1, 8) == -1 && target->ReadPixel(1, 16, 8) == int((entt::entity)cube),
              "sky keeps the background ID and metal keeps its entity ID");
        Check(glm::length(sky) > .001f && color(1) == sky, "switching IBL keeps the same daylight sky");
        Check(glm::distance(metal, color(16)) > .001f, "switching IBL changes the lit metal");
        std::cout << (editor ? "editor" : "runtime") << " sky RGB " << sky.r << ',' << sky.g << ',' << sky.b
                  << " metal red with/without IBL " << metal.r << '/' << color(16).r << '\n';
    }
    scene.SetEnvironment({});
    draw(true);
    Check(color(1) == glm::vec3(0), "removing the environment restores the clear color");
}

static void CheckFoxScene()
{
    Ref<Scene> scene = CreateRef<Scene>();
    scene->OnViewportResize(48,48);
    if (!SceneSerializer(scene).Deserialize("assets/scenes/Character-Fox.redflag")) {
        Check(false, "Open the glTF animation scene in OpenGL");
        return;
    }
    Entity fox = scene->GetEntityByUUID(102);
    ModelAnimationComponent& player = fox.GetComponent<ModelAnimationComponent>();
    player.Playing = false;
    const glm::mat4 view = glm::lookAt(glm::vec3(3.3f,1.6f,1.2f), glm::vec3(0,.8f,0), glm::vec3(0,1,0));
    FramebufferSpecification spec;
    spec.Width = 48; spec.Height = 48;
    spec.Attachments = {FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER,
                        FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH32F};
    Ref<Framebuffer> target = Framebuffer::Create(spec);
    SceneRenderer::SetMainSceneColorTarget(target->GetColorAttachmentRendererID(),48,48);
    auto draw = [&](float seconds) {
        player.Time = seconds;
        scene->OnUpdateAnimations(0.f);
        target->Bind();
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        target->ClearAttachment(1,-1);
        SceneRenderer::RenderRuntime(*scene, glm::perspective(.65f,1.f,.05f,25.f), glm::inverse(view));
        std::array<int,48*48> ids;
        glGetTextureImage(target->GetColorAttachmentRendererID(1),0,GL_RED_INTEGER,GL_INT,sizeof(ids),ids.data());
        return ids;
    };
    const auto first = draw(0.f), next = draw(.2f), paused = draw(.2f);
    int visible = 0, changed = 0;
    const int foxID = int(static_cast<entt::entity>(fox));
    for (size_t i = 0; i < first.size(); ++i) {
        visible += next[i] == foxID;
        changed += (next[i] == foxID) != (first[i] == foxID);
    }
    Check(visible > 0 && changed > 0 && next == paused && glGetError() == GL_NO_ERROR,
          "OpenGL Fox skin changes the rendered silhouette and holds its paused pose");
    std::cout << "OpenGL Fox Walk at 0.2 s: " << visible << " actor pixels; " << changed << " changed pixels\n";
    std::ofstream("../output/validation/character-animation/fox-opengl.csv")
        << "scene,seconds,actor_pixels,changed_pixels,paused_identical\nCharacter-Fox,0.2,"
        << visible << ',' << changed << ',' << (paused == next) << '\n';
    AssetManager::Unload<Model>(fox.GetComponent<ModelRendererComponent>().ModelID);
}

static void CheckGlassScene()
{
    const char* path = "../bin-int/Tests/glass-materials.redflag";
    Ref<Scene> scene = CreateRef<Scene>();
    Entity pane = scene->CreateEntity("Glass");
    Ref<Material> glass = Material::Create({ .3f, .6f, .9f, .4f });
    glass->Alpha = AlphaMode::Blend;
    glass->DoubleSided = true;
    glass->RefractionIndex = 1.5f;
    glass->Thickness = .125f;
    pane.AddComponent<MeshRendererComponent>().Mat = glass;

    Entity model = scene->CreateEntity("Frame");
    ModelRendererComponent& frame = model.AddComponent<ModelRendererComponent>();
    frame.FilePath = "assets/models/Primitives/Plane.obj";
    frame.OverrideMaterials = { nullptr, glass->Clone() };
    frame.OverrideMaterials[1]->Alpha = AlphaMode::Mask;
    frame.OverrideMaterials[1]->AlphaCutoff = .25f;
    SceneSerializer(scene).Serialize(path);

    Ref<Scene> restored = CreateRef<Scene>();
    const bool loaded = SceneSerializer(restored).Deserialize(path);
    Check(loaded, "reopen a scene containing transparent materials");
    if (!loaded) return;
    const Material& surface = *restored->GetEntityByUUID(pane.GetUUID()).GetComponent<MeshRendererComponent>().Mat;
    Check(surface.Alpha == AlphaMode::Blend && surface.BaseColor == glass->BaseColor &&
          surface.DoubleSided && surface.RefractionIndex == 1.5f && surface.Thickness == .125f,
          "glass keeps its color, sidedness, index of refraction and thickness");
    const auto& parts = restored->GetEntityByUUID(model.GetUUID()).GetComponent<ModelRendererComponent>().OverrideMaterials;
    Check(parts.size() == 2 && !parts[0] && parts[1] && parts[1]->Alpha == AlphaMode::Mask &&
          parts[1]->AlphaCutoff == .25f, "empty override inherits the model; mask keeps its threshold");
}

static void CheckLampScene()
{
    const char* path = "../bin-int/Tests/lamp-material.redflag";
    Ref<Scene> scene = CreateRef<Scene>();
    Entity entity = scene->CreateEntity("Lamp");
    auto& lamp = entity.AddComponent<ModelRendererComponent>();
    lamp.FilePath = "assets/models/LightPanel/LightPanel.gltf";
    lamp.ModelID = AssetManager::Add<Model>(lamp.FilePath);
    lamp.ModelAsset = AssetManager::Load<Model>(lamp.ModelID);
    const Ref<Material>& original = lamp.ModelAsset->GetSubMeshes().at(0).Mat;
    Ref<Material> cover = original->Clone();
    lamp.OverrideMaterials = {cover};
    for (glm::vec3 emission : { glm::vec3(3, .5f, .1f), glm::vec3(0) })
    {
        cover->Emission = emission;
        SceneSerializer(scene).Serialize(path);
        Ref<Scene> restored = CreateRef<Scene>();
        Check(SceneSerializer(restored).Deserialize(path), "reopen a lamp edited in the inspector");
        const auto& edited = restored->GetEntityByUUID(entity.GetUUID()).GetComponent<ModelRendererComponent>();
        const Ref<Material>& material = edited.OverrideMaterials.at(0);
        Check(material->Emission == emission && original->EmissiveMap &&
              material->EmissiveMap == original->EmissiveMap, "lamp keeps its embedded stripes, including after turning it off");
    }
    // 从磁盘选一张替换图，再移除它。重开场景后分别检查这两种选择。
    const std::filesystem::path file = std::filesystem::absolute("../bin-int/Tests/lamp.ppm");
    std::ofstream(file, std::ios::binary) << "P6\n1 1\n255\n" << "RGB";
    cover->Emission = glm::vec3(1);
    for (bool useFile : {true, false})
    {
        cover->EmissiveMap = useFile ? Texture2D::Create(file.string()) : nullptr;
        SceneSerializer(scene).Serialize(path);
        Ref<Scene> restored = CreateRef<Scene>();
        Check(SceneSerializer(restored).Deserialize(path), "reopen a lamp after changing its emission texture");
        const auto& parts = restored->GetEntityByUUID(entity.GetUUID()).GetComponent<ModelRendererComponent>().OverrideMaterials;
        const Ref<Texture2D>& map = parts.at(0)->EmissiveMap;
        Check(useFile ? map && std::filesystem::equivalent(map->GetPath(), file) && std::filesystem::path(map->GetPath()).is_relative() : !map,
              "replacement image uses a relative path; removing it keeps the panel uniformly lit");
    }
    AssetManager::Unload<Model>(lamp.ModelID);
}

static int ReadHistoryReset()
{
    GLint buffer;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 8, &buffer);
    int reset;
    glGetNamedBufferSubData(buffer, 132, sizeof(reset), &reset);
    return reset;
}

static void CheckEmission()
{
    FramebufferSpecification spec;
    spec.Width = 64; spec.Height = 16;
    spec.Attachments = {FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER,
        FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH32F};
    auto target = Framebuffer::Create(spec);
    SceneRenderer::SetMainSceneColorTarget(target->GetColorAttachmentRendererID(), 64, 16);
    auto mesh = Mesh::CreateCube();
    const uint8_t pattern[] = {128, 64, 255, 0};
    auto stripe = Texture2D::Create(1, 1);
    stripe->SetData((void*)pattern, sizeof(pattern));
    auto green = Texture2D::Create(1, 1);
    uint8_t greenPixel[] = {0, 255, 0, 0};
    green->SetData(greenPixel, sizeof(greenPixel));

    // 前两块共用贴图，只换各自的发光颜色。后两块用不同贴图或无贴图，单独绘制。
    const glm::vec3 emission[] = {{1,2,3}, {3,1,.25f}, {4,.5f,2}, {7,2.5f,5}};
    const Ref<Texture2D> textures[] = {stripe, stripe, {}, green};
    Scene scene;
    std::array<int,4> ids;
    for (int i=0; i<4; ++i)
    {
        auto entity = scene.CreateEntity("Panel");
        ids[i] = int((entt::entity)entity);
        auto& transform = entity.GetComponent<TransformComponent>();
        transform.Translation.x = float(i)-1.5f;
        transform.Scale = {.8f,.8f,.1f};
        auto& component = entity.AddComponent<MeshRendererComponent>();
        component.MeshData = mesh;
        component.Mat = Material::Create(glm::vec4(0,0,0,1));
        component.Mat->Emission = emission[i];
        component.Mat->EmissiveMap = textures[i];
    }
    target->Bind();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    target->ClearAttachment(1,-1);
    auto projection = glm::ortho(-2.f,2.f,-.5f,.5f,.1f,8.f);
    SceneRenderer::RenderRuntime(scene, projection, glm::translate(glm::mat4(1),{0,0,2}));
    target->Bind(); // 后处理已切换绘制目标，拾取要读原来那张实体编号图。

    // 输入字节 128、64 对应线性 0.21586、0.05127。发光图的 alpha 不影响模型拾取。
    glm::vec3 patternRGB(.21586f,.05127f,1);
    const glm::vec3 expected[] = {emission[0]*patternRGB, emission[1]*patternRGB, emission[2], {0,2.5f,0}};
    for (int i=0; i<4; ++i)
    {
        glm::vec4 pixel;
        glGetTextureSubImage(target->GetColorAttachmentRendererID(),0,8+16*i,8,0,1,1,1,
            GL_RGBA,GL_FLOAT,sizeof(pixel),&pixel);
        std::cout << "OpenGL light panel " << i << " HDR: " << pixel.r << ", " << pixel.g << ", " << pixel.b << '\n';
        Check(glm::length(glm::vec3(pixel)-expected[i]) < .012f, "unlit emission matches the material and sRGB texel");
        Check(target->ReadPixel(1,8+16*i,8) == ids[i], "emissive pixels keep their own entity IDs");
    }
    auto& calls = Renderer3D::GetDrawCallLog();
    auto count=std::count_if(calls.begin(),calls.end(),[](const auto& draw){
        return draw.Pass == Renderer3D::DrawCallInfo::PassType::Main;
    });
    Check(count == 3, "only panels sharing the same emission map are instanced together");
    Check(glGetError() == GL_NO_ERROR, "OpenGL emission texture views are valid");

    Scene imported;
    auto panel = imported.CreateEntity("Lamp");
    panel.GetComponent<TransformComponent>().Rotation.x = glm::radians(180.f);
    auto& source = panel.AddComponent<ModelRendererComponent>();
    source.ModelAsset = Model::Load("../tests/fixtures/EmissivePanel.gltf");
    target->Bind();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    SceneRenderer::RenderRuntime(imported, projection, glm::translate(glm::mat4(1), {0, 0, 2}));

    // 底图的 UV 固定在红色。发光图用另一套 UV，才会在右半边读到绿色。
    glm::vec4 left, right;
    const auto image = target->GetColorAttachmentRendererID();
    glGetTextureSubImage(image, 0, 27, 8, 0, 1, 1, 1, GL_RGBA, GL_FLOAT, sizeof(left), &left);
    glGetTextureSubImage(image, 0, 37, 8, 0, 1, 1, 1, GL_RGBA, GL_FLOAT, sizeof(right), &right);
    std::cout << "OpenGL glTF UV1: left red " << left.r << ", right green " << right.g << '\n';
    Check(left.r > 3.98f && left.g < .01f && right.r < .01f && std::abs(right.g - .4317f) < .01f,
          "imported emitter uses its own glTF texture coordinates");
    target->Unbind();
}

static void CheckRSMSamples(int count)
{
    GLint buffer;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 6, &buffer);
    glm::vec4 params[129];
    glGetNamedBufferSubData(buffer, 0, sizeof(params), params);
    bool coversDisk = params[0].x == count;
    for (int i = 0; i < count; ++i)
    {
        const glm::vec2 offset(params[i + 1]);
        coversDisk &= std::abs(glm::dot(offset, offset) - (i + .5f) / count) < 1e-5f;
    }
    Check(coversDisk, "RSM sample count preserves uniform disk coverage");
    Check(params[0].w == 2 * SceneRenderer::GetShadowSettings().OrthoHalfSize,
          "RSM receives the full light projection width in world units");
}

static void TestTemporalProjection()
{
    // 本帧中心像素的值是 0.25，周围有 0 和 1。上一帧的 0.75 落在这个范围里，可以直接混合。
    // 上一帧占九成时，中心应读到 0.25 × 0.1 + 0.75 × 0.9 = 0.70。
    float current[9] = { 0, 1, 0, 1, .25f, 1, 0, 1, 0 };
    float history[9], depth[9];
    std::fill_n(history, 9, .75f);
    std::fill_n(depth, 9, .5f);
    GLuint textures[3];
    glCreateTextures(GL_TEXTURE_2D, 3, textures);
    const float* pixels[] = { current, history, depth };
    for (int i = 0; i < 3; ++i)
    {
        glTextureStorage2D(textures[i], 1, GL_R32F, 3, 3);
        glTextureSubImage2D(textures[i], 0, 0, 0, 3, 3, GL_RED, GL_FLOAT, pixels[i]);
        glTextureParameteri(textures[i], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(textures[i], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(textures[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(textures[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    FramebufferSpecification spec;
    spec.Width = spec.Height = 3;
    spec.Attachments = { FramebufferTextureFormat::RGBA16F };
    auto target = Framebuffer::Create(spec);
    auto shader = Shader::Create("assets/shaders/RSMTemporalResolve.glsl");
    struct TemporalParams
    {
        glm::mat4 InvViewProj{ 1 }, PrevViewProj{ 1 };
        float BlendAlpha = .9f;
        int ResetHistory = 0;
        float Padding[2]{};
    } params;
    static_assert(sizeof(TemporalParams) == 144);
    auto uniform = UniformBuffer::Create(sizeof(params), 8);
    GLuint vao, buffer;
    const float triangle[] = { -1, -1, 3, -1, -1, 3 };
    glCreateVertexArrays(1, &vao);
    glCreateBuffers(1, &buffer);
    glNamedBufferData(buffer, sizeof(triangle), triangle, GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(vao, 0, buffer, 0, sizeof(float) * 2);
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(vao, 0, 0);
    const auto draw = [&]()
    {
        target->Bind();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        for (int i = 0; i < 3; ++i) glBindTextureUnit(i, textures[i]);
        uniform->SetData(&params, sizeof(params));
        shader->Bind();
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        float color[4];
        glGetTextureSubImage(target->GetColorAttachmentRendererID(), 0, 1, 1, 0, 1, 1, 1,
                             GL_RGBA, GL_FLOAT, sizeof(color), color);
        return color[0];
    };
    Check(std::abs(draw() - .7f) < .001f, "temporal resolve blends valid reprojected history");
    params.ResetHistory = 1;
    Check(std::abs(draw() - .25f) < .001f, "temporal reset uses only the current frame");
    params.ResetHistory = 0;
    const glm::vec3 outside[] = { { 3, 0, 0 }, { 0, 3, 0 }, { 0, 0, 3 }, { 0, 0, -3 } };
    for (const auto& translation : outside)
    {
        params.PrevViewProj = glm::translate(glm::mat4(1), translation);
        const float value = draw();
        std::cout << "Previous clip offset: " << translation.x << ',' << translation.y << ','
                  << translation.z << " resolved=" << value << '\n';
        Check(std::abs(value - .25f) < .001f, "temporal resolve rejects all previous clip boundaries");
    }
    params.PrevViewProj = glm::mat4(1);
    params.PrevViewProj[3][3] = -1;
    Check(std::abs(draw() - .25f) < .001f, "temporal resolve rejects points behind the previous camera");
    glBindVertexArray(0);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &buffer);
    glDeleteTextures(3, textures);
    glEnable(GL_DEPTH_TEST);
    Check(glGetError() == GL_NO_ERROR, "temporal projection fixtures produce no GL errors");
}

#include "NpcPickupTests.h"
#include "NpcWorldTests.h"
#include "NpcRuntimeSmoke.h"
#include "NpcMotionTests.h"
#include "TerrainTests.h"
#include "TerrainTraversalTests.h"
#include "AnimationNotifyTests.h"

int main(int argc, char** argv)
{
    Log::Init();
    if (argc == 2 && std::string(argv[1]) == "--animation-events")
    {
        CheckAnimationEvents();
        return s_Failures ? 1 : 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--traversal")
    {
        CheckTraversal();
        return s_Failures ? 1 : 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--terrain")
    {
        CheckTerrain();
        CheckQuarryTerrain();
        return s_Failures ? 1 : 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--terrain-resave")
    {
        ResaveTerrain(argv[2], argv[3]);
        return s_Failures ? 1 : 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--station-reach") {
        CheckStationWorkDistance(Check);
        CheckGroundRescue(Check);
        CheckStationFollowCorner(Check);
        return s_Failures ? 1 : 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--npc-animation")
    {
        CheckNpcMotion();
        return s_Failures ? 1 : 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--npc-sdk")
    {
        CheckNpcRuntime(static_cast<uint16_t>(std::stoi(argv[2])), std::filesystem::u8path(argv[3]));
        return s_Failures ? 1 : 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--npc-game")
        return CheckCompanionGame(static_cast<uint16_t>(std::stoi(argv[2])), std::string(argv[3]) == "stocked") ? 0 : 1;
    CheckWorldActions(Check);
    CheckNpcPickup(Check);
    CheckPhysicsScene(Check);
    if (argc == 2 && std::string(argv[1]) == "--physics")
        return s_Failures ? 1 : 0;
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(33, 17, "SceneRenderTests", nullptr, nullptr);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return 1;
    RenderCommand::Init();
    Renderer2D::Init();
    Renderer3D::Init();
    SceneRenderer::Init();
    CheckGlassScene();
    CheckLampScene();
    SceneRenderer::GetIBLEnabled() = false;
    SceneRenderer::GetShadowEnabled() = false;
    SceneRenderer::GetRSMSettings().Enabled = false;
    SceneRenderer::GetBloomSettings().Enabled = false;
    CheckSkySettings();
    CheckDaylight();
    CheckFoxScene();
    CheckEmission();
    {
        FramebufferSpecification spec;
        spec.Width = 33; spec.Height = 17;
        spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER,
                             FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH32F };
        auto target = Framebuffer::Create(spec);
        SceneRenderer::SetMainSceneColorTarget(target->GetColorAttachmentRendererID(), 33, 17);
        Scene scene;
        auto& registry = scene.GetRegistry();
        auto sprite = registry.create();
        registry.emplace<TransformComponent>(sprite);
        registry.emplace<SpriteRendererComponent>(sprite, glm::vec4(1, 0, 0, .5f));
        EditorCamera camera(45, 33.0f / 17, .1f, 100);
        for (int mode = 0; mode < 2; ++mode)
        {
            for (int frame = 0; frame < 2; ++frame)
            {
                target->Bind();
                glDepthMask(GL_TRUE);
                glClearColor(0, 0, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                target->ClearAttachment(1, -1);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                if (mode == 0) SceneRenderer::RenderRuntime(scene, glm::mat4(1), glm::mat4(1));
                else SceneRenderer::RenderScene(scene, camera);
                Check(glGetError() == GL_NO_ERROR, "editor/runtime SSAO accepts depth-only attachment");
                GLint alignment;
                glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
                Check(alignment == 4, "depth preview preserves upload row alignment");
                float color[4];
                glGetTextureSubImage(target->GetColorAttachmentRendererID(), 0, 16, 8, 0, 1, 1, 1,
                                     GL_RGBA, GL_FLOAT, sizeof(color), color);
                Check(std::abs(color[0] - .5f) < .01f, "sprite alpha blending survives consecutive scene frames");
                float normal[4];
                glGetTextureSubImage(target->GetColorAttachmentRendererID(2), 0, 16, 8, 0, 1, 1, 1,
                                     GL_RGBA, GL_FLOAT, sizeof(normal), normal);
                std::cout << "Sprite normal: " << normal[0] << ',' << normal[1] << ',' << normal[2] << '\n';
                Check(normal[0] == 0 && normal[1] == 0 && normal[2] == 0,
                      "2D sprites do not supply undefined normals to SSAO");
                unsigned char preview[4]{};
                glGetTextureSubImage(SceneRenderer::GetPrepassDepthTextureID(), 0, 16, 8, 0, 1, 1, 1,
                                     GL_RED, GL_UNSIGNED_BYTE, sizeof(preview), preview);
                const GLenum previewError = glGetError();
                std::cout << "Depth preview: mode=" << mode << " frame=" << frame
                          << " value=" << int(preview[0]) << " GL error=" << previewError << '\n';
                Check(previewError == GL_NO_ERROR && preview[0] == 0,
                      "single-depth geometry remains visible in depth preview");
            }
        }
        registry.destroy(sprite);
        auto light = registry.create();
        registry.emplace<TransformComponent>(light);
        auto& directional = registry.emplace<DirectionalLightComponent>(light);
        directional.Direction = glm::vec3(0);
        SceneRenderer::GetShadowEnabled() = true;
        target->Bind();
        SceneRenderer::RenderRuntime(scene, glm::mat4(1), glm::mat4(1));
        GLint lightBuffer;
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, LIGHT_UBO_BINDING, &lightBuffer);
        GPULightUBO lightHeader{};
        glGetNamedBufferSubData(lightBuffer, 0, sizeof(lightHeader), &lightHeader);
        Check(lightHeader.NumDirectionalLights == 0, "zero direction does not upload NaN light data");
        directional.Direction = glm::vec3(0, -1, 0);
        // 后建实体先被 EnTT 遍历；主绘制只支持一盏方向光，因此阴影不能改用第二盏。
        auto unshadowedLight = registry.create();
        registry.emplace<TransformComponent>(unshadowedLight);
        registry.emplace<DirectionalLightComponent>(unshadowedLight).CastShadow = false;
        target->Bind();
        SceneRenderer::RenderRuntime(scene, glm::mat4(1), glm::mat4(1));
        GLint cameraBuffer;
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &cameraBuffer);
        glm::vec4 shadowParams;
        glGetNamedBufferSubData(cameraBuffer, 144, sizeof(shadowParams), &shadowParams);
        Check(shadowParams.w == 0, "shadow selection matches the uploaded directional light");
        registry.destroy(unshadowedLight);
        SceneRenderer::GetRSMSettings().Enabled = true;
        for (int mode = 0; mode < 2; ++mode)
        {
            target->Bind();
            glDepthMask(GL_TRUE);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (mode == 0) SceneRenderer::RenderRuntime(scene, glm::mat4(1), glm::mat4(1));
            else SceneRenderer::RenderScene(scene, camera);
            Check(glGetError() == GL_NO_ERROR, "editor/runtime RSM accepts depth-only attachment");
            Check(SceneRenderer::GetRSMResolvedTextureID() != 0, "RSM temporal output exists");
        }
        auto& rsm = SceneRenderer::GetRSMSettings();
        glm::mat4 projection(1), cameraTransform(1);
        const auto render = [&]()
        {
            target->Bind();
            glDepthMask(GL_TRUE);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            SceneRenderer::RenderRuntime(scene, projection, cameraTransform);
        };
        render();
        Check(ReadHistoryReset() == 1, "editor to runtime transition resets RSM history");
        render();
        Check(ReadHistoryReset() == 0, "stable RSM inputs reuse history");
        cameraTransform = glm::translate(glm::mat4(1), glm::vec3(.1f, 0, 0));
        render();
        Check(ReadHistoryReset() == 0, "ordinary camera motion preserves RSM reprojection");
        projection[0][0] = 2;
        render();
        Check(ReadHistoryReset() == 1, "projection changes reset RSM history");
        rsm.Enabled = false;
        render();
        Check(SceneRenderer::GetRSMResolvedTextureID() == 0, "disabling RSM invalidates resolved history");
        rsm.Enabled = true;
        render();
        Check(ReadHistoryReset() == 1, "re-enabling RSM starts with the current frame");
        rsm.Intensity *= .5f;
        render();
        Check(ReadHistoryReset() == 1, "RSM intensity changes discard old lighting");
        rsm.SampleRadius *= .5f;
        render();
        Check(ReadHistoryReset() == 1, "RSM radius changes discard old lighting");
        for (int count : { 16, 32, 128 })
        {
            rsm.NumSamples = count;
            render();
            Check(ReadHistoryReset() == 1, "RSM sample count changes reset history");
            CheckRSMSamples(count);
        }
        directional.Color = glm::vec3(.5f, 1, 1);
        render();
        Check(ReadHistoryReset() == 1, "directional color changes discard old lighting");
        directional.Intensity *= 2;
        render();
        Check(ReadHistoryReset() == 1, "directional intensity changes discard old lighting");
        directional.Direction = glm::vec3(1, -1, 0);
        render();
        Check(ReadHistoryReset() == 1, "directional orientation changes discard old lighting");
        SceneRenderer::GetShadowSettings().OrthoHalfSize *= 2;
        render();
        Check(ReadHistoryReset() == 1, "light coverage changes reset RSM history");
        CheckRSMSamples(128);
        {
            Scene otherScene;
            auto& otherRegistry = otherScene.GetRegistry();
            auto otherLight = otherRegistry.create();
            otherRegistry.emplace<TransformComponent>(otherLight);
            otherRegistry.emplace<DirectionalLightComponent>(otherLight, directional);
            target->Bind();
            SceneRenderer::RenderRuntime(otherScene, projection, cameraTransform);
            Check(ReadHistoryReset() == 1, "another scene does not inherit RSM history");
        }
        Check(glGetError() == GL_NO_ERROR, "RSM history and sampling fixtures produce no GL errors");
        SceneRenderer::Shutdown();
        Check(SceneRenderer::GetPrepassDepthTextureID() == 0 && SceneRenderer::GetViewNormalTextureID() == 0,
              "shutdown clears borrowed preview texture names");
    }
    Renderer3D::Shutdown();
    Renderer2D::Shutdown();
    TestTemporalProjection();
    Check(glGetError() == GL_NO_ERROR, "scene GPU resources released without GL errors");
    glfwDestroyWindow(window);
    glfwTerminate();
    return s_Failures ? 1 : 0;
}
