#include "rfpch.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Renderer/Framebuffer.h"
#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Renderer2D.h"
#include "Redflag/Renderer/Renderer3D.h"
#include "Redflag/Renderer/SceneRenderer.h"
#include "Redflag/Scene/Scene.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Scene/SceneSerializer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <stb_image_write.h>
#include "RenderDocCapture.h"

using namespace Redflag;
namespace fs = std::filesystem;

static glm::vec3 ReadVector(const YAML::Node& node)
{
    return { node[0].as<float>(), node[1].as<float>(), node[2].as<float>() };
}

static void Require(bool passed, const char* message)
{
    if (!passed)
        throw std::runtime_error(message);
}

// 诊断只保存附件的原始浮点值；文件按左上角起始逐行排列。
static YAML::Node SaveFloatTexture(uint32_t texture, const fs::path& file, bool depth = false)
{
    int width, height;
    glGetTextureLevelParameteriv(texture, 0, GL_TEXTURE_WIDTH, &width);
    glGetTextureLevelParameteriv(texture, 0, GL_TEXTURE_HEIGHT, &height);
    const int channels = depth ? 1 : 4;
    std::vector<float> values(size_t(width) * height * channels);
    glGetTextureImage(texture, 0, depth ? GL_DEPTH_COMPONENT : GL_RGBA, GL_FLOAT,
        static_cast<GLsizei>(values.size() * sizeof(float)), values.data());
    for (int y = 0; y < height / 2; ++y)
        std::swap_ranges(values.begin() + y * width * channels, values.begin() + (y + 1) * width * channels,
            values.begin() + (height - 1 - y) * width * channels);
    std::ofstream stream(file, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(float));
    YAML::Node result;
    result["File"] = file.filename().generic_string();
    result["Width"] = width;
    result["Height"] = height;
    result["Channels"] = channels;
    result["Format"] = "little-endian float32, top-left origin";
    return result;
}

// 节点变换仍由 Model 保存；这里只给整个模型统一尺度并移到世界原点。
static void FitModel(const Model& model, float size, TransformComponent& transform)
{
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    glm::mat4 rotation = glm::toMat4(glm::quat(transform.Rotation));
    for (const auto& part : model.GetSubMeshes())
    {
        const auto& low = part.MeshData->GetAABBMin();
        const auto& high = part.MeshData->GetAABBMax();
        glm::mat4 matrix = rotation * part.LocalTransform;
        for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
                for (int z = 0; z < 2; ++z)
                {
                    glm::vec3 point = matrix * glm::vec4(x ? high.x : low.x,
                        y ? high.y : low.y, z ? high.z : low.z, 1.0f);
                    minimum = glm::min(minimum, point);
                    maximum = glm::max(maximum, point);
                }
    }
    glm::vec3 extent = maximum - minimum;
    float scale = size / std::max({ extent.x, extent.y, extent.z });
    transform.Scale = glm::vec3(scale);
    transform.Translation = -(minimum + maximum) * (0.5f * scale);
}

static void RenderGallery(YAML::Node settings, const fs::path& configPath, RenderDocCapture& capture)
{
    const int width = settings["Size"][0].as<int>();
    const int height = settings["Size"][1].as<int>();
    const auto modelPath = fs::absolute(configPath.parent_path() / settings["Model"].as<std::string>());
    const auto output = settings["Output"]
        ? fs::absolute(configPath.parent_path() / settings["Output"].as<std::string>())
        : fs::absolute("../output/scenes");
    const auto name = settings["Name"].as<std::string>();
    fs::create_directories(output);

    FramebufferSpecification spec;
    spec.Width = width;
    spec.Height = height;
    spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER,
        FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH32F };
    auto target = Framebuffer::Create(spec);
    SceneRenderer::SetMainSceneColorTarget(target->GetColorAttachmentRendererID(), width, height);
    SceneRenderer::GetIBLEnabled() = false;
    SceneRenderer::GetShadowEnabled() = true;
    SceneRenderer::GetZPrepassEnabled() = true;
    SceneRenderer::GetSSAOEnabled() = settings["SSAO"].as<bool>();
    SceneRenderer::GetBloomSettings().Enabled = false;
    SceneRenderer::GetToneMapSettings().Exposure = settings["Exposure"].as<float>();
    SceneRenderer::GetShadowSettings().OrthoHalfSize = settings["ShadowHalfSize"].as<float>();
    SceneRenderer::GetRSMSettings().Enabled = settings["RSM"].as<bool>();
    SceneRenderer::GetRSMSettings().Intensity = settings["RSMIntensity"].as<float>();
    SceneRenderer::GetRSMSettings().SampleRadius = settings["RSMRadius"].as<float>();
    if (settings["RSMBlendAlpha"])
        SceneRenderer::GetRSMBlendAlpha() = settings["RSMBlendAlpha"].as<float>();

    auto scene = CreateRef<Scene>();
    scene->OnViewportResize(width, height);
    // 示例实体使用固定 UUID，重复运行生成同一份可编辑场景。
    auto modelEntity = scene->CreateEntityWithUUID(UUID(1), name);
    auto& transform = modelEntity.GetComponent<TransformComponent>();
    transform.Rotation = glm::radians(ReadVector(settings["ModelRotation"]));
    auto& renderer = modelEntity.AddComponent<ModelRendererComponent>();
    renderer.FilePath = fs::relative(modelPath).generic_string();
    renderer.ModelID = AssetManager::Add<Model>(modelPath.generic_string());
    renderer.ModelAsset = AssetManager::Load<Model>(renderer.ModelID);
    Require(renderer.ModelAsset && !renderer.ModelAsset->GetSubMeshes().empty(), "Model import failed");
    FitModel(*renderer.ModelAsset, settings["ModelSize"].as<float>(), transform);

    auto sunlight = scene->CreateEntityWithUUID(UUID(2), "Directional Light");
    auto& directional = sunlight.AddComponent<DirectionalLightComponent>();
    directional.Direction = ReadVector(settings["Direction"]);
    directional.Intensity = settings["LightIntensity"].as<float>();
    if (settings["PointIntensity"].as<float>() > 0)
    {
        auto point = scene->CreateEntityWithUUID(UUID(3), "Point Light");
        point.GetComponent<TransformComponent>().Translation = ReadVector(settings["PointPosition"]);
        auto& light = point.AddComponent<PointLightComponent>();
        light.Intensity = settings["PointIntensity"].as<float>();
        light.Radius = settings["PointRadius"].as<float>();
    }

    glm::mat4 view = glm::lookAt(ReadVector(settings["Eye"]), ReadVector(settings["Target"]), glm::vec3(0, 1, 0));
    auto cameraEntity = scene->CreateEntityWithUUID(UUID(4), "Main Camera");
    auto& cameraTransform = cameraEntity.GetComponent<TransformComponent>();
    cameraTransform.Translation = ReadVector(settings["Eye"]);
    cameraTransform.Rotation = glm::eulerAngles(glm::quat_cast(glm::inverse(view)));
    auto& camera = cameraEntity.AddComponent<CameraComponent>().Camera;
    camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
    camera.SetPerspectiveFOV(glm::radians(settings["Fov"].as<float>()));
    camera.SetPerspectiveNearClip(settings["Near"].as<float>());
    camera.SetPerspectiveFarClip(settings["Far"].as<float>());
    scene->OnViewportResize(width, height);

    // 先保存再重新打开，下面绘制的正是 Forge 可以加载的场景。
    fs::create_directories("assets/scenes");
    const auto scenePath = (settings["Output"] ? output : fs::path("assets/scenes"))
        / ("Reference-" + name + ".redflag");
    SceneSerializer(scene).Serialize(scenePath.generic_string());
    auto reopened = CreateRef<Scene>();
    reopened->OnViewportResize(width, height);
    Require(SceneSerializer(reopened).Deserialize(scenePath.generic_string()), "Saved scene cannot reopen");
    reopened->OnViewportResize(width, height);
    auto reopenedCamera = reopened->GetPrimaryCameraEntity();
    Require(reopenedCamera, "Saved scene has no primary camera");
    const glm::mat4 projection = reopenedCamera.GetComponent<CameraComponent>().Camera.GetProjection();
    const glm::mat4 cameraMatrix = reopenedCamera.GetComponent<TransformComponent>().GetTransform();
    const int frames = settings["Frames"].as<int>();
    for (int frame = 0; frame < frames; ++frame)
    {
        if (frame == frames - 1)
            capture.Begin();
        target->Bind();
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDisable(GL_CULL_FACE);
        glClearColor(0.015f, 0.018f, 0.025f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        target->ClearAttachment(1, -1);
        SceneRenderer::RenderRuntime(*reopened, projection, cameraMatrix);
        Require(glGetError() == GL_NO_ERROR, "SceneRenderer GL error");
        if (frame == frames - 1)
            capture.End();
    }

    // 保存真实后处理输出；仅翻转 OpenGL 的行方向，不调整图像颜色。
    const uint32_t color = SceneRenderer::GetFinalColorTextureID();
    Require(color != 0, "Postprocess output is missing");
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    glGetTextureImage(color, 0, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(pixels.size()), pixels.data());
    for (int y = 0; y < height / 2; ++y)
        std::swap_ranges(pixels.begin() + y * width * 4, pixels.begin() + (y + 1) * width * 4,
            pixels.begin() + (height - 1 - y) * width * 4);
    Require(stbi_write_png((output / (name + ".png")).string().c_str(), width, height, 4,
        pixels.data(), width * 4) != 0, "PNG write failed");

    std::vector<float> hdr(pixels.size());
    glGetTextureImage(target->GetColorAttachmentRendererID(), 0, GL_RGBA, GL_FLOAT,
        static_cast<GLsizei>(hdr.size() * sizeof(float)), hdr.data());
    Require(std::all_of(hdr.begin(), hdr.end(), [](float value) { return std::isfinite(value); }), "HDR contains NaN/Inf");
    std::vector<int> ids(size_t(width) * height);
    glGetTextureImage(target->GetColorAttachmentRendererID(1), 0, GL_RED_INTEGER, GL_INT,
        static_cast<GLsizei>(ids.size() * sizeof(int)), ids.data());
    const auto visiblePixels = std::count_if(ids.begin(), ids.end(), [](int id) { return id != -1; });
    Require(visiblePixels > 100, "Scene geometry is not visible");
    Require(glGetError() == GL_NO_ERROR, "Attachment readback failed");

    if (settings["Readback"] && settings["Readback"].as<bool>())
    {
        auto attachments = settings["Result"]["Attachments"];
        attachments["HDR"] = SaveFloatTexture(target->GetColorAttachmentRendererID(), output / (name + "-HDR.f32"));
        attachments["Depth"] = SaveFloatTexture(target->GetDepthAttachmentRendererID(), output / (name + "-Depth.f32"), true);
        attachments["ViewNormal"] = SaveFloatTexture(target->GetColorAttachmentRendererID(2), output / (name + "-ViewNormal.f32"));
        if (settings["RSM"].as<bool>())
        {
            int worldPosition;
            glGetIntegeri_v(GL_TEXTURE_BINDING_2D, 14, &worldPosition);
            attachments["RSMFlux"] = SaveFloatTexture(SceneRenderer::GetRSMFluxTextureID(), output / (name + "-Flux.f32"));
            attachments["RSMWorldPosition"] = SaveFloatTexture(worldPosition, output / (name + "-WorldPosition.f32"));
            attachments["RSMNormal"] = SaveFloatTexture(SceneRenderer::GetRSMNormalTextureID(), output / (name + "-Normal.f32"));
            attachments["RSMResolved"] = SaveFloatTexture(SceneRenderer::GetRSMResolvedTextureID(), output / (name + "-Resolved.f32"));
        }
        Require(glGetError() == GL_NO_ERROR, "Diagnostic readback failed");
    }

    const auto statistics = Renderer3D::GetStats();
    settings["Result"]["GPU"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    settings["Result"]["OpenGL"] = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    settings["Result"]["Submeshes"] = renderer.ModelAsset->GetSubMeshes().size();
    settings["Result"]["Vertices"] = statistics.VertexCount;
    settings["Result"]["Indices"] = statistics.IndexCount;
    settings["Result"]["DrawCalls"] = statistics.DrawCalls;
    settings["Result"]["Culled"] = statistics.CulledCount;
    settings["Result"]["VisiblePixels"] = visiblePixels;
    settings["Result"]["HDRFinite"] = true;
    settings["Result"]["Scene"] = scenePath.generic_string();
    settings["Result"]["ReopenedSceneRendered"] = true;
    settings["Result"]["ModelID"] = renderer.ModelID;
    settings["Result"]["Scale"] = transform.Scale.x;
    settings["Result"]["Translation"] = std::vector<float>{ transform.Translation.x, transform.Translation.y, transform.Translation.z };
    settings["Result"]["ImageSource"] = "SceneRenderer final LDR texture, direct readback";
    std::ofstream record(output / (name + ".yaml"));
    record << settings;
    std::cout << "Rendered " << name << ": " << visiblePixels << " geometry pixels, "
              << statistics.DrawCalls << " draws; output " << (output / (name + ".png")) << '\n';
}

int main(int argc, char** argv)
{
    if (argc != 2 && (argc != 4 || std::string(argv[2]) != "--capture"))
    {
        std::cerr << "Usage: SceneGallery path/to/scene.yaml [--capture path/to/renderdoc.dll]\n";
        return 1;
    }
    const auto config = fs::absolute(argv[1]);
    auto settings = YAML::LoadFile(config.string());
    const auto name = settings["Name"].as<std::string>();
    RenderDocCapture capture(argc == 4 ? argv[3] : nullptr,
        fs::path("../output/scenes") / name, name + " - SceneRenderer",
        "Real imported model, rendered through SceneRenderer after scene serialization and reopening. "
        "Camera, lights and feature switches are recorded alongside the PNG. No GPU timing claim.");
    Log::Init();
    Require(glfwInit() != 0, "GLFW init failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(64, 64, "Redflag Scene Gallery", nullptr, nullptr);
    Require(window != nullptr, "GL context failed");
    glfwMakeContextCurrent(window);
    Require(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress) != 0, "GL loader failed");
    RenderCommand::Init();
    Renderer2D::Init();
    Renderer3D::Init();
    SceneRenderer::Init();
    int result = 0;
    try
    {
        RenderGallery(settings, config, capture);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    SceneRenderer::Shutdown();
    AssetManager::ClearAll();
    Renderer3D::Shutdown();
    Renderer2D::Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
