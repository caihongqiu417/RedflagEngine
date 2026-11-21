#include "rfpch.h"
#include "Redflag/Core/Log.h"
#include "Redflag/Renderer/Framebuffer.h"
#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Renderer2D.h"
#include "RenderDocCapture.h"
#include "DrawTiming.h"
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <array>

using namespace Redflag;

constexpr int Grid = 8;
constexpr int Size = 256;
constexpr int SpriteCount = Grid * Grid;
constexpr char Output[] = "../output/profiling/batch2d/";

struct Sprite
{
    SpriteRendererComponent Component;
    glm::mat4 Transform{ 1.0f };
};

using Sprites = std::array<Sprite, SpriteCount>;

/* 两张像素贴图分别画出黄色菱形和蓝色十字，透明处露出背景。 */
static Ref<Texture2D> MakeIcon(bool diamond)
{
    std::array<uint32_t, 9 * 9> pixels{};
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x)
        {
            int dx = std::abs(x - 4), dy = std::abs(y - 4);
            if (diamond && dx + dy <= 4)
                pixels[y * 9 + x] = dx + dy < 2 ? 0xffb9faff : 0xff64cdff;
            if (!diamond && (dx <= 1 || dy <= 1))
                pixels[y * 9 + x] = dx + dy < 2 ? 0xffffeed6 : 0xffe8be65;
        }
    auto texture = Texture2D::Create(9, 9);
    texture->SetData(pixels.data(), sizeof(pixels));
    glTextureParameteri(texture->GetRendererID(), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(texture->GetRendererID(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return texture;
}

static Sprites MakeSprites()
{
    auto diamond = MakeIcon(true), cross = MakeIcon(false);
    Sprites sprites;
    for (int i = 0; i < SpriteCount; ++i)
    {
        int x = i % Grid, y = i / Grid;
        auto& sprite = sprites[i];
        sprite.Transform[0][0] = sprite.Transform[1][1] = 1.5f / Grid;
        sprite.Transform[3][0] = (2.0f * x + 1) / Grid - 1;
        sprite.Transform[3][1] = (2.0f * y + 1) / Grid - 1;
        sprite.Component.Texture = (x + y) % 2 == 0 ? diamond : cross;
    }
    return sprites;
}

static void Clear()
{
    const float background[4] = { 0.035f, 0.055f, 0.09f, 1.0f };
    const int empty[4] = { -1, 0, 0, 0 };
    glClearBufferfv(GL_COLOR, 0, background);
    glClearBufferiv(GL_COLOR, 1, empty);
}

static void Draw(Sprites& sprites, int batchSize)
{
    for (int first = 0; first < SpriteCount; first += batchSize)
    {
        Renderer2D::BeginScene(glm::mat4(1.0f), glm::mat4(1.0f));
        for (int i = first; i < first + batchSize; ++i)
            Renderer2D::DrawSprite(sprites[i].Transform, sprites[i].Component, i + 1);
        Renderer2D::EndScene();
    }
}

static void Capture(Sprites& sprites, RenderDocCapture& capture)
{
    capture.Begin();
    for (int batch : { 1, SpriteCount })
    {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1,
            batch == 1 ? "Sprites: individual submissions" : "Sprites: shared batch");
        Clear();
        Renderer2D::ResetStats();
        Draw(sprites, batch);
        const auto stats = Renderer2D::GetStats();
        assert(stats.QuadCount == SpriteCount && stats.DrawCalls == SpriteCount / batch);
        glPopDebugGroup();
    }
    capture.End();
}

int main(int argc, char** argv)
{
    const bool captureFrame = argc == 3 && std::string(argv[1]) == "--capture";
    const bool measure = argc == 2 && std::string(argv[1]) == "--measure";
    if (!captureFrame && !measure)
    {
        std::cerr << "Usage: SpriteBatchProfile --capture path/to/renderdoc.dll | --measure\n";
        return 1;
    }
    std::filesystem::create_directories(Output);
    RenderDocCapture capture(captureFrame ? argv[2] : nullptr, std::string(Output) + "Sprites",
        "Renderer2D: 64 individual draws and one shared batch",
        "Two passes clear and draw the same 8x8 sprites. Color and entity ID must match.");
    Log::Init();
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto window = glfwCreateWindow(Size, Size, "SpriteBatchProfile", nullptr, nullptr);
    if (!window)
        return 1;
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        return 1;
    std::cout << glGetString(GL_RENDERER) << ", " << glGetString(GL_VERSION) << '\n';
    RenderCommand::Init();
    Renderer2D::Init();
    {
        FramebufferSpecification spec;
        spec.Width = spec.Height = Size;
        spec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER };
        auto target = Framebuffer::Create(spec);
        target->Bind();
        glDisable(GL_DEPTH_TEST);
        auto sprites = MakeSprites();
        if (captureFrame) Capture(sprites, capture);
        else
            MeasureDraws(std::filesystem::path(Output) / "timing.csv",
                std::array<const char*, 2>{ "individual", "batch" }, [&](size_t mode) {
                    Clear();
                    Draw(sprites, mode == 0 ? 1 : SpriteCount);
                });
        assert(glGetError() == GL_NO_ERROR);
    }
    Renderer2D::Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
}
