#include "rfpch.h"

#include"Redflag.h"
#include "ForgeLayer.h"
#include "CharacterInput.h"
#include "ColliderGizmos.h"
#include "EditorEnvironment.h"

#include "Platform/OpenGL/OpenGLShader.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Redflag/Renderer/Shader.h"
#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/SceneRenderer.h"
#include "Redflag/Renderer/IBLPrecompute.h"
#include"Redflag/Scene/CameraControllerScript.h"
#include "Redflag/Scene/SceneSerializer.h"
#include "Redflag/Utils/PlatformUtils.h"

#include"Redflag/Renderer/EditorCamera.h"

#include"ImGuizmo.h"
#include"Redflag/Math/Math.h"


namespace Redflag {

    // ----------
    // 编辑器 Gizmo 绘制辅助函数
    // 在 Renderer2D::BeginScene / EndScene 之间调用
    // ----------

    static void DrawCameraGizmo(const TransformComponent& tc, const glm::vec4& color)
    {
        glm::mat4 camT = tc.GetTransform();
        auto xf = [&](glm::vec3 v) -> glm::vec3 {
            return glm::vec3(camT * glm::vec4(v, 1.0f));
        };

        // 机身矩形（-Z 为朝向）
        float bw = 0.18f, bh = 0.12f, bd = 0.10f;
        glm::vec3 f0 = xf({ -bw, -bh, -bd }), f1 = xf({  bw, -bh, -bd });
        glm::vec3 f2 = xf({  bw,  bh, -bd }), f3 = xf({ -bw,  bh, -bd });
        glm::vec3 b0 = xf({ -bw, -bh,  bd }), b1 = xf({  bw, -bh,  bd });
        glm::vec3 b2 = xf({  bw,  bh,  bd }), b3 = xf({ -bw,  bh,  bd });
        Renderer2D::DrawLine(f0, f1, color); Renderer2D::DrawLine(f1, f2, color);
        Renderer2D::DrawLine(f2, f3, color); Renderer2D::DrawLine(f3, f0, color);
        Renderer2D::DrawLine(b0, b1, color); Renderer2D::DrawLine(b1, b2, color);
        Renderer2D::DrawLine(b2, b3, color); Renderer2D::DrawLine(b3, b0, color);
        Renderer2D::DrawLine(f0, b0, color); Renderer2D::DrawLine(f1, b1, color);
        Renderer2D::DrawLine(f2, b2, color); Renderer2D::DrawLine(f3, b3, color);

        // 镜头锥台
        float lx = 0.28f, ly = 0.20f, lz = -0.36f;
        glm::vec3 l0 = xf({ -lx, -ly, lz }), l1 = xf({  lx, -ly, lz });
        glm::vec3 l2 = xf({  lx,  ly, lz }), l3 = xf({ -lx,  ly, lz });
        Renderer2D::DrawLine(l0, l1, color); Renderer2D::DrawLine(l1, l2, color);
        Renderer2D::DrawLine(l2, l3, color); Renderer2D::DrawLine(l3, l0, color);
        glm::vec3 fc = xf({ 0.0f, 0.0f, -bd });
        Renderer2D::DrawLine(fc, l0, color); Renderer2D::DrawLine(fc, l1, color);
        Renderer2D::DrawLine(fc, l2, color); Renderer2D::DrawLine(fc, l3, color);
    }

    static void DrawDirectionalLightGizmo(const glm::vec3& pos, const glm::vec3& direction, const glm::vec4& color)
    {
        glm::vec3 dir = glm::normalize(direction);
        glm::vec3 up = (glm::abs(dir.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 right = glm::normalize(glm::cross(dir, up));
        glm::vec3 forward = glm::cross(right, dir);

        constexpr int segments = 8;
        constexpr float radius = 0.25f;
        for (int i = 0; i < segments; i++)
        {
            float a0 = glm::two_pi<float>() * i / segments;
            float a1 = glm::two_pi<float>() * (i + 1) / segments;
            Renderer2D::DrawLine(
                pos + right * (radius * cosf(a0)) + forward * (radius * sinf(a0)),
                pos + right * (radius * cosf(a1)) + forward * (radius * sinf(a1)), color);
        }

        float rayLen = 1.2f;
        for (int i = 0; i < 4; i++)
        {
            float angle = glm::two_pi<float>() * i / 4;
            glm::vec3 base = pos + right * (radius * cosf(angle)) + forward * (radius * sinf(angle));
            Renderer2D::DrawLine(base, base + dir * rayLen, color);
        }
        Renderer2D::DrawLine(pos, pos + dir * rayLen, color);
    }

    // 小球标出点光源的位置，虚线圈标出它能照到多远。
    static void DrawPointLightGizmo(const glm::vec3& pos, float radius, const glm::vec4& color)
    {
        // 灯的照明半径可以调整，中心小球始终画成 0.15 个场景单位，方便辨认。
        constexpr float bulbR = 0.15f;
        constexpr int   bulbSegs = 12;
        for (int i = 0; i < bulbSegs; ++i)
        {
            float a0 = glm::two_pi<float>() * i / bulbSegs;
            float a1 = glm::two_pi<float>() * (i + 1) / bulbSegs;
            float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
            Renderer2D::DrawLine(pos + glm::vec3(bulbR * c0, bulbR * s0, 0.0f),
                                 pos + glm::vec3(bulbR * c1, bulbR * s1, 0.0f), color);
            Renderer2D::DrawLine(pos + glm::vec3(bulbR * c0, 0.0f, bulbR * s0),
                                 pos + glm::vec3(bulbR * c1, 0.0f, bulbR * s1), color);
            Renderer2D::DrawLine(pos + glm::vec3(0.0f, bulbR * c0, bulbR * s0),
                                 pos + glm::vec3(0.0f, bulbR * c1, bulbR * s1), color);
        }

        // 八条短线与球面留一点距离，让点光源图标和普通线框球容易区分。
        constexpr float rayInner = bulbR * 1.15f;
        constexpr float rayOuter = bulbR * 2.2f;
        const glm::vec3 rayDirs[8] = {
            { 1, 0, 0}, {-1, 0, 0}, {0,  1, 0}, {0, -1, 0},
            { 1, 0, 1}, {-1, 0, 1}, {1,  0,-1}, {-1, 0,-1},
        };
        for (const auto& raw : rayDirs)
        {
            glm::vec3 d = glm::normalize(raw);
            Renderer2D::DrawLine(pos + d * rayInner, pos + d * rayOuter, color);
        }

        // 两个圈靠得太近时只画灯的图标。能分开看清时，再用浅色虚线圈画出照明范围。
        if (radius > bulbR * 3.0f)
        {
            glm::vec4 dim = color;
            dim.a *= 0.4f;

            constexpr int rSegs = 32;
            for (int i = 0; i < rSegs; ++i)
            {
                // 每隔一段画一段（虚线效果）
                if ((i & 1) == 0) continue;
                float a0 = glm::two_pi<float>() * i / rSegs;
                float a1 = glm::two_pi<float>() * (i + 1) / rSegs;
                Renderer2D::DrawLine(
                    pos + glm::vec3(radius * cosf(a0), 0.0f, radius * sinf(a0)),
                    pos + glm::vec3(radius * cosf(a1), 0.0f, radius * sinf(a1)), dim);
            }

            // 在圆周的四个方向各画一条竖线，从侧面看也能看出照明范围。
            const float pole = radius * 0.08f;
            Renderer2D::DrawLine(pos + glm::vec3( radius, 0, 0), pos + glm::vec3( radius, pole, 0), dim);
            Renderer2D::DrawLine(pos + glm::vec3(-radius, 0, 0), pos + glm::vec3(-radius, pole, 0), dim);
            Renderer2D::DrawLine(pos + glm::vec3(0, 0,  radius), pos + glm::vec3(0, pole,  radius), dim);
            Renderer2D::DrawLine(pos + glm::vec3(0, 0, -radius), pos + glm::vec3(0, pole, -radius), dim);
        }
    }

    ForgeLayer::ForgeLayer()
        : Layer("ForgeLayer"), m_CameraController(2560.0f / 1440.0f, true)
    {
    }

    void ForgeLayer::OnAttach()
    {
        RF_PROFILE_FUNCTION();

        // 加载纹理资源
        m_RedCloudTexture     = Texture2D::Create("assets/textures/RedCloud.png");
        m_CheckerboardTexture = Texture2D::Create("assets/textures/Checkerboard.png");
        m_IconPlay            = Texture2D::Create("assets/icons/PlayButton.png");
        m_IconStop            = Texture2D::Create("assets/icons/StopButton.png");
        m_IconSimulate        = Texture2D::Create("assets/icons/SimulateButton.png");

        // 初始化正交相机控制器缩放
        m_CameraController.SetZoomLevel(6.f);

        // 每个像素保存颜色、物体编号、相机看到的表面法线和深度，顺序与下面的格式一致。
        // 颜色用浮点数保存，亮度超过 1 的部分仍可用于计算光晕。鼠标选物体时读的是编号。
        FramebufferSpecification fbSpec;
        fbSpec.Attachments = { FramebufferTextureFormat::RGBA16F,
                               FramebufferTextureFormat::RED_INTEGER,
                               FramebufferTextureFormat::RGBA16F,
                               FramebufferTextureFormat::Depth };
        fbSpec.Width = m_ViewportSize.x;
        fbSpec.Height = m_ViewportSize.y;
        m_Framebuffer = Framebuffer::Create(fbSpec);

        // 3D 测试：创建立方体 Mesh
        m_CubeMesh = Mesh::CreateCube();

        // 初始化编辑场景
        m_EditorScene = CreateRef<Scene>();
        m_EditorScene->SetEnvironment(DefaultSky);
        m_EditorScene->OnViewportResize(m_ViewportSize.x, m_ViewportSize.y);
        m_ActiveScene = m_EditorScene;
        m_SceneHierachyPanel.SetContext(m_EditorScene);

        m_EditorCamera = EditorCamera(30.f, 1.788f, 0.1f, 1000.f);
    }

    void ForgeLayer::OnDetach()
    {
        RF_PROFILE_FUNCTION();
        if (m_SceneState == SceneState::Play)
            OnSceneStop();
        else if (m_SceneState == SceneState::Simulate)
            OnSceneSimulateStop();
    }

    void ForgeLayer::OnUpdate(Timestep ts)
    {
        RF_PROFILE_FUNCTION();

        // 仅在 Viewport 聚焦时更新相机
        if (m_ViewportFocused)
        {
            m_CameraController.OnUpdate(ts);
            m_EditorCamera.OnUpdate(ts);
        }

        Renderer2D::ResetStats();

        // 先把所有像素的物体编号清为 -1。画完后，鼠标指到背景仍会读到 -1。
        m_Framebuffer->Bind();
        RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        RenderCommand::Clear();
        m_Framebuffer->ClearAttachment(1, -1);
        // 后处理需要从这张场景颜色图取亮度来画光晕，把图和它的尺寸交给 SceneRenderer。
        {
            const auto& spec = m_Framebuffer->GetSpecifictaion();
            SceneRenderer::SetMainSceneColorTarget(
                m_Framebuffer->GetColorAttachmentRendererID(0),
                (int)spec.Width,
                (int)spec.Height);
        }
        // 背景没有表面，把法线清成零。SSAO 读到零就跳过这个像素，保留原来的亮度。
        m_Framebuffer->ClearAttachment(2, 0);

        // 按场景状态分发更新逻辑
        switch (m_SceneState)
        {
            // Simulate：物理步进 + EditorCamera 渲染，不执行脚本
            case SceneState::Simulate:
            {
                m_ActiveScene->OnUpdateSimulation(ts, m_EditorCamera, ReadCharacterMovement(m_ViewportFocused));
                break;
            }

            // Edit：编辑器渲染，叠加碰撞体线框和相机图标
            case SceneState::Edit:
            {
                m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);

                // 编辑器覆盖层
                Renderer2D::BeginScene(m_EditorCamera);
                Renderer2D::SetLineWidth(1.5f);

                // 绘制所有 CameraComponent 的相机线框图标
                auto camView = m_ActiveScene->GetRegistry().view<TransformComponent, CameraComponent>();
                camView.each([&](auto entity, TransformComponent& tc, CameraComponent& cc)
                {
                    Entity ent = { entity, m_ActiveScene.get() };
                    Entity sel = m_SceneHierachyPanel.GetSelectedEntity();
                    glm::vec4 color = (sel && sel == ent)
                        ? glm::vec4(0.95f, 0.25f, 0.25f, 1.0f)
                        : glm::vec4(1.0f,  1.0f,  1.0f, 0.85f);
                    DrawCameraGizmo(tc, color);
                });

                // 绘制所有方向光 Gizmo
                {
                    auto dlView = m_ActiveScene->GetRegistry().view<TransformComponent, DirectionalLightComponent>();
                    dlView.each([&](auto entity, TransformComponent& tc, DirectionalLightComponent& dl)
                    {
                        Entity ent = { entity, m_ActiveScene.get() };
                        Entity sel = m_SceneHierachyPanel.GetSelectedEntity();
                        glm::vec4 color = (sel && sel == ent)
                            ? glm::vec4(1.0f, 0.85f, 0.0f, 1.0f)
                            : glm::vec4(1.0f, 0.9f, 0.4f, 0.85f);
                        glm::vec3 worldDir = glm::normalize(glm::mat3(tc.GetTransform()) * dl.Direction);
                        DrawDirectionalLightGizmo(tc.Translation, worldDir, color);
                    });
                }

                // 绘制所有点光源 Gizmo
                {
                    auto plView = m_ActiveScene->GetRegistry().view<TransformComponent, PointLightComponent>();
                    plView.each([&](auto entity, TransformComponent& tc, PointLightComponent& pl)
                    {
                        Entity ent = { entity, m_ActiveScene.get() };
                        Entity sel = m_SceneHierachyPanel.GetSelectedEntity();
                        glm::vec4 color = (sel && sel == ent)
                            ? glm::vec4(0.2f, 0.8f, 1.0f, 1.0f)
                            : glm::vec4(0.5f, 0.85f, 1.0f, 0.7f);
                        DrawPointLightGizmo(tc.Translation, pl.Radius, color);
                    });
                }

                // 碰撞体可视化：绿色线框，选中时高亮为黄色
                DrawColliderGizmos(*m_ActiveScene, m_SceneHierachyPanel.GetSelectedEntity(), m_ShowPhysicsColliders,
                    [](glm::vec3 a, glm::vec3 b, bool selected) {
                        Renderer2D::DrawLine(a, b, selected ? glm::vec4(1.f,.8f,.27f,1.f) : glm::vec4(.34f,.86f,.69f,.8f));
                    });
                if (m_ShowPhysicsColliders)
                {
                    Entity sel = m_SceneHierachyPanel.GetSelectedEntity();

                    // BoxCollider2D：矩形线框
                    auto bcView = m_ActiveScene->GetRegistry().view<TransformComponent, BoxCollider2DComponent>();
                    bcView.each([&](auto entity, TransformComponent& tc, BoxCollider2DComponent& bc)
                    {
                        Entity ent = { entity, m_ActiveScene.get() };
                        glm::vec4 col = (sel && sel == ent)
                            ? glm::vec4(1.0f, 0.7f, 0.0f, 1.0f)
                            : glm::vec4(0.0f, 1.0f, 0.0f, 0.8f);

                        glm::mat4 t = tc.GetTransform();
                        auto xf = [&](glm::vec3 v) { return glm::vec3(t * glm::vec4(v, 1.0f)); };

                        float ox = bc.Offset.x, oy = bc.Offset.y;
                        float sx = bc.Size.x,   sy = bc.Size.y;
                        glm::vec3 p0 = xf({ ox - sx, oy - sy, 0.001f });
                        glm::vec3 p1 = xf({ ox + sx, oy - sy, 0.001f });
                        glm::vec3 p2 = xf({ ox + sx, oy + sy, 0.001f });
                        glm::vec3 p3 = xf({ ox - sx, oy + sy, 0.001f });

                        Renderer2D::DrawLine(p0, p1, col);
                        Renderer2D::DrawLine(p1, p2, col);
                        Renderer2D::DrawLine(p2, p3, col);
                        Renderer2D::DrawLine(p3, p0, col);
                    });

                    // CircleCollider2D：32 段折线近似圆形
                    auto ccView = m_ActiveScene->GetRegistry().view<TransformComponent, CircleCollider2DComponent>();
                    ccView.each([&](auto entity, TransformComponent& tc, CircleCollider2DComponent& cc)
                    {
                        Entity ent = { entity, m_ActiveScene.get() };
                        glm::vec4 col = (sel && sel == ent)
                            ? glm::vec4(1.0f, 0.7f, 0.0f, 1.0f)
                            : glm::vec4(0.0f, 1.0f, 0.0f, 0.8f);

                        glm::mat4 t = tc.GetTransform();
                        auto xf = [&](glm::vec3 v) { return glm::vec3(t * glm::vec4(v, 1.0f)); };

                        constexpr int   segments = 32;
                        constexpr float twoPi    = glm::two_pi<float>();
                        float ox = cc.Offset.x, oy = cc.Offset.y;
                        float r  = cc.Radius;

                        for (int i = 0; i < segments; ++i)
                        {
                            float a0 = twoPi * (float)i       / (float)segments;
                            float a1 = twoPi * (float)(i + 1) / (float)segments;
                            glm::vec3 v0 = xf({ ox + r * glm::cos(a0), oy + r * glm::sin(a0), 0.001f });
                            glm::vec3 v1 = xf({ ox + r * glm::cos(a1), oy + r * glm::sin(a1), 0.001f });
                            Renderer2D::DrawLine(v0, v1, col);
                        }
                    });
                }

                Renderer2D::EndScene();
                break;
            }

            // Play：完整运行时（脚本 + 物理 + 场景主相机渲染）
            case SceneState::Play:
            {
                m_ActiveScene->OnUpdateRuntime(ts, ReadCharacterMovement(m_ViewportFocused));
                break;
            }
        }

        // 后处理把结果画到了另一张图上。读鼠标下的物体编号前，要切回保存编号的场景帧缓冲。
        m_Framebuffer->Bind();

        // 鼠标位置以整个窗口为准，先减去场景面板左上角的位置。
        auto [mx, my] = ImGui::GetMousePos();
        mx -= m_ViewportBounds[0].x;
        my -= m_ViewportBounds[0].y;
        glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
        if (mx >= 0 && my >= 0 && mx < viewportSize.x && my < viewportSize.y)
        {
            const int mouseX = int(mx * m_ViewportSize.x / viewportSize.x);
            const int mouseY = int(m_ViewportSize.y) - 1 - int(my * m_ViewportSize.y / viewportSize.y);
            if (!ImGuizmo::IsUsing())
            {
                int pixelData = m_Framebuffer->ReadPixel(1, mouseX, mouseY);
                m_LastHoveredEntityID = pixelData;
                if (pixelData != -1)
                {
                    entt::entity e = (entt::entity)pixelData;
                    if (m_ActiveScene->GetRegistry().valid(e))
                        m_HoveredEntity = { e, m_ActiveScene.get() };
                    else
                        m_HoveredEntity = {};
                }
                else
                {
                    m_HoveredEntity = {};
                }
            }
        }
        else
        {
            m_LastHoveredEntityID = -1;
            m_HoveredEntity = {};
        }

        m_Framebuffer->Unbind();
    }

    void ForgeLayer::DrawEnvironmentMenu()
    {
        if (!ImGui::BeginMenu(u8"环境贴图", m_SceneState == SceneState::Edit)) return;
        bool visible = m_EditorScene->IsSkyVisible();
        if (ImGui::MenuItem(u8"显示天空背景", nullptr, &visible))
            m_EditorScene->SetSkyVisible(visible);
        if (ImGui::MenuItem(u8"使用默认天空"))
        {
            m_EditorScene->SetEnvironment(DefaultSky);
            m_EnvironmentMessage.clear();
        }
        if (ImGui::MenuItem(u8"载入 HDR 全景…"))
        {
            const std::string path = FileDialogs::OpenFile("HDR (*.hdr)\0*.hdr\0");
            if (!path.empty())
            {
                const bool loaded = IBLPrecompute::Reload(path);
                if (loaded) m_EditorScene->SetEnvironment(path);
                m_EnvironmentMessage = loaded ? u8"环境贴图已生成。" : u8"无法读取这张 HDR 图。";
            }
        }
        if (ImGui::MenuItem(u8"保存烘焙结果…", nullptr, false, IBLPrecompute::IsReady()))
        {
            const std::string path = FileDialogs::SaveFile("Redflag IBL (*.rfibl)\0*.rfibl\0", "rfibl");
            if (!path.empty())
            {
                const bool saved = IBLPrecompute::SaveBaked(path);
                if (saved) m_EditorScene->SetEnvironment(path);
                m_EnvironmentMessage = saved ? u8"烘焙结果已保存。保存场景后，下次打开会直接使用它。" : u8"写入失败，请检查所选目录。";
            }
        }
        if (ImGui::MenuItem(u8"使用烘焙文件…"))
        {
            const std::string path = FileDialogs::OpenFile("Redflag IBL (*.rfibl)\0*.rfibl\0");
            if (!path.empty())
            {
                const bool loaded = IBLPrecompute::LoadBaked(path);
                if (loaded) m_EditorScene->SetEnvironment(path);
                m_EnvironmentMessage = loaded ? u8"已载入烘焙环境光。" : u8"文件不完整或版本不匹配。";
            }
        }
        if (ImGui::MenuItem(u8"移除环境贴图", nullptr, false, !m_EditorScene->GetEnvironment().empty()))
        {
            m_EditorScene->SetEnvironment({});
            m_EnvironmentMessage.clear();
        }
        if (!m_EnvironmentMessage.empty()) ImGui::TextDisabled("%s", m_EnvironmentMessage.c_str());
        ImGui::EndMenu();
    }

    void ForgeLayer::OnImGuiRender()
    {
        RF_PROFILE_FUNCTION();

        // 全屏 DockSpace 设置
        static bool opt_fullscreen = true;
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

        if (opt_fullscreen)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 10.0f));
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 12.0f));
        ImGui::Begin("DockSpace Demo", nullptr, window_flags);
        if (opt_fullscreen) ImGui::PopStyleVar(3);
        ImGui::PopStyleVar();

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f));
        }

        // MenuBar：File 菜单 / View 菜单 / 工具栏按钮
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New",        "Ctrl+N"))        NewScene();
                if (ImGui::MenuItem("Open...",    "Ctrl+O"))        OpenScene();
                if (ImGui::MenuItem("Save",       "Ctrl+S"))        SaveScene();
                if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) SaveSceneAs();
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) Application::Get().Close();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                m_DevContext.DrawMenu();
                ImGui::MenuItem("Physics Colliders", nullptr, &m_ShowPhysicsColliders);
                ImGui::MenuItem("Show Culling AABBs",nullptr, &Renderer3D::GetDebugCullingVizEnabled());
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(u8"渲染效果"))
            {
                ImGui::MenuItem(u8"环境照明 (IBL)",             nullptr, &SceneRenderer::GetIBLEnabled());
                DrawEnvironmentMenu();
                ImGui::MenuItem(u8"阴影",          nullptr, &SceneRenderer::GetShadowEnabled());
                ImGui::MenuItem(u8"深度预写 (Pre-Z)",       nullptr, &SceneRenderer::GetZPrepassEnabled());
                ImGui::MenuItem(u8"环境光遮蔽 (SSAO)",            nullptr, &SceneRenderer::GetSSAOEnabled());
                ImGui::MenuItem(u8"间接光 (RSM)",             nullptr, &SceneRenderer::GetRSMSettings().Enabled);
                ImGui::MenuItem(u8"视野外物体剔除", nullptr, &Renderer3D::GetFrustumCullingEnabled());
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(u8"后处理"))
            {
                ImGui::MenuItem(u8"泛光 (Bloom)", nullptr, &SceneRenderer::GetBloomSettings().Enabled);
                ImGui::EndMenu();
            }

            // 工具栏：Play / Simulate 两个图标按钮居中排列
            {
                float requestedIconSize = 24.0f;
                float frameH    = ImGui::GetFrameHeight();
                float maxIcon   = std::max(4.0f, frameH - 2.0f);
                float iconSize  = requestedIconSize > maxIcon ? maxIcon : requestedIconSize;
                float btnSpacing = 15.0f;

                float cursorStartX = ImGui::GetCursorPosX();
                float cursorStartY = ImGui::GetCursorPosY();
                float availW  = ImGui::GetContentRegionAvail().x;
                float startX  = cursorStartX + availW * 0.5f - (iconSize * 2 + btnSpacing) * 0.5f;
                float offsetY = (frameH - iconSize) * 0.5f;

                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 0.4f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));

                // Play / Stop 按钮
                // Edit->Play：触发 OnScenePlay()
                // Play->Edit：触发 OnSceneStop()
                // Simulate 时此按钮禁用
                {
                    bool inSimulate = (m_SceneState == SceneState::Simulate);
                    if (inSimulate) ImGui::BeginDisabled();

                    Ref<Texture2D> playIcon = (m_SceneState == SceneState::Play) ? m_IconStop : m_IconPlay;
                    ImGui::SetCursorPosX(startX);
                    ImGui::SetCursorPosY(cursorStartY + offsetY);
                    if (ImGui::ImageButton("##playstop", (ImTextureID)(uint64_t)playIcon->GetRendererID(),
                        ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0)))
                    {
                        if (m_SceneState == SceneState::Edit) OnScenePlay();
                        else                                  OnSceneStop();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip((m_SceneState == SceneState::Play) ? "Stop" : "Play");

                    if (inSimulate) ImGui::EndDisabled();
                }

                // Simulate / Stop Simulate 按钮（橙色 tint 与 Play 按钮区分）
                // Edit->Simulate：触发 OnSceneSimulate()
                // Simulate->Edit：触发 OnSceneSimulateStop()
                // Play 时此按钮禁用
                {
                    bool inPlay = (m_SceneState == SceneState::Play);
                    if (inPlay) ImGui::BeginDisabled();

                    bool simActive = (m_SceneState == SceneState::Simulate);
                    Ref<Texture2D> simIcon = simActive ? m_IconStop : m_IconSimulate;
                    ImVec4 simTint = simActive ? ImVec4(1, 1, 1, 1) : ImVec4(1.0f, 0.6f, 0.1f, 1.0f);
                    ImGui::SetCursorPosX(startX + iconSize + btnSpacing);
                    ImGui::SetCursorPosY(cursorStartY + offsetY);
                    if (ImGui::ImageButton("##simulate", (ImTextureID)(uint64_t)simIcon->GetRendererID(),
                        ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0),
                        simTint))
                    {
                        if (m_SceneState == SceneState::Edit) OnSceneSimulate();
                        else                                  OnSceneSimulateStop();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(simActive ? "Stop Simulate" : "Simulate");

                    if (inPlay) ImGui::EndDisabled();
                }

                ImGui::PopStyleColor(3);
            }
            ImGui::EndMenuBar();
        }

        // Profile / Stats 面板
        {
            RF_PROFILE_SCOPE("Stats");

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 6));
            ImGui::Begin("Profile");

            const ImVec4 colRed       = ImVec4(0.80f, 0.08f, 0.08f, 1.0f);
            const ImVec4 colWhite     = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
            const ImVec4 colDimWhite  = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
            const ImVec4 colHighlight = ImVec4(0.85f, 0.15f, 0.15f, 1.0f);
            const ImVec4 colSeparator = ImVec4(0.70f, 0.10f, 0.10f, 0.70f);

            Entity selectedEntity = m_SceneHierachyPanel.GetSelectedEntity();

            std::string hoveredName = "None";
            if (m_HoveredEntity)
                hoveredName = m_HoveredEntity.GetComponent<TagComponent>().Tag;

            std::string selectedName = "None";
            if (selectedEntity)
                selectedName = selectedEntity.GetComponent<TagComponent>().Tag;

            ImGui::PushStyleColor(ImGuiCol_Text, colDimWhite);
            ImGui::Text("Hovered ");
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 0);
            ImGui::PushStyleColor(ImGuiCol_Text, m_HoveredEntity ? colHighlight : colDimWhite);
            ImGui::Text("%s", hoveredName.c_str());
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, colDimWhite);
            ImGui::Text("Selected");
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 4);
            ImGui::PushStyleColor(ImGuiCol_Text, selectedEntity ? colWhite : colDimWhite);
            ImGui::Text("%s", selectedName.c_str());
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Separator, colSeparator);
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Spacing();

            auto stats2D = Renderer2D::GetStats();
            auto stats3D = Renderer3D::GetStats();
            uint32_t totalDrawCalls = stats2D.DrawCalls + stats3D.DrawCalls;
            uint32_t totalVertices  = stats2D.GetTotalVertexCount() + stats3D.VertexCount;
            uint32_t totalIndices   = stats2D.GetTotalIndexCount()  + stats3D.IndexCount;

            ImGui::PushStyleColor(ImGuiCol_Text, colRed);
            ImGui::Text("Render Stats");
            ImGui::PopStyleColor();
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Text, colWhite);
            ImGui::Text("Draw Calls  %d", totalDrawCalls);
            ImGui::Text("Vertices    %d", totalVertices);
            ImGui::Text("Indices     %d", totalIndices);
            float fps = ImGui::GetIO().Framerate;
            // 面板每半秒更新一次读数，留出看清帧率的时间。
            m_FpsAccumTime += ImGui::GetIO().DeltaTime;
            if (m_FpsAccumTime >= 0.5f || m_DisplayedFPS == 0.0f)
            {
                m_DisplayedFPS     = fps;
                m_DisplayedFrameMs = 1000.0f / (fps > 0.0f ? fps : 1.0f);
                m_FpsAccumTime     = 0.0f;
            }
            ImGui::Text("FPS         %.1f  (%.2f ms)", m_DisplayedFPS, m_DisplayedFrameMs);
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Separator, colSeparator);
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Spacing();

            float halfW = ImGui::GetContentRegionAvail().x * 0.5f;

            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Text, colRed);
            ImGui::Text("2D");
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, colDimWhite);
            ImGui::Text("Calls   %d", stats2D.DrawCalls);
            ImGui::Text("Quads   %d", stats2D.QuadCount);
            ImGui::Text("Verts   %d", stats2D.GetTotalVertexCount());
            ImGui::Text("Idx     %d", stats2D.GetTotalIndexCount());
            ImGui::PopStyleColor();
            ImGui::EndGroup();

            ImGui::SameLine(halfW);

            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Text, colRed);
            ImGui::Text("3D");
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, colDimWhite);
            ImGui::Text("Calls   %d", stats3D.DrawCalls);
            ImGui::Text("Cmds    %d", stats3D.SubmittedCommands);
            ImGui::Text("Culled  %d", stats3D.CulledCount);
            ImGui::Text("Verts   %d", stats3D.VertexCount);
            ImGui::Text("Idx     %d", stats3D.IndexCount);
            ImGui::PopStyleColor();
            ImGui::EndGroup();

            ImGui::End();
            ImGui::PopStyleVar(2);
        }

        // 面板按绘制顺序列出 Renderer3D 记下的调用。
        {
            ImGui::Begin("Draw Calls");
            const auto& log = Renderer3D::GetDrawCallLog();
            ImGui::Text("Total  %d", (int)log.size());

            static int s_PassFilter = 0;
            const char* filters[] = { "All", "Main", "Shadow", "Prepass", "SSAO", "RSM",
                "BloomBright", "BloomUp", "BloomTonemap", "Transparency" };
            ImGui::Combo("Pass", &s_PassFilter, filters, IM_ARRAYSIZE(filters));

            if (ImGui::BeginTable("dc_table", 4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                ImVec2(0, 300)))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
                ImGui::TableSetupColumn("#",         ImGuiTableColumnFlags_WidthFixed, 32);
                ImGui::TableSetupColumn("Pass",      ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableSetupColumn("Entity",    ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Tris",      ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableHeadersRow();
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("IndexCount / 3");

                int row = 0;
                for (size_t i = 0; i < log.size(); ++i)
                {
                    const auto& dc = log[i];
                    int pass = static_cast<int>(dc.Pass) + 1;
                    if (s_PassFilter && s_PassFilter != pass) continue;

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", row++);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(filters[pass]);

                    ImGui::TableSetColumnIndex(2);
                    if (dc.Pass == Renderer3D::DrawCallInfo::PassType::SSAO ||
                        dc.Pass == Renderer3D::DrawCallInfo::PassType::BloomBright ||
                        dc.Pass == Renderer3D::DrawCallInfo::PassType::BloomUp ||
                        dc.Pass == Renderer3D::DrawCallInfo::PassType::BloomTonemap ||
                        (dc.Pass == Renderer3D::DrawCallInfo::PassType::Transparency && dc.MeshPtr == 0))
                    {
                        ImGui::TextDisabled("(fullscreen)");
                    }
                    else if (dc.EntityID >= 0 && m_ActiveScene)
                    {
                        Entity e { (entt::entity)dc.EntityID, m_ActiveScene.get() };
                        if (e && e.HasComponent<TagComponent>())
                            ImGui::Text("%d %s", dc.EntityID, e.GetComponent<TagComponent>().Tag.c_str());
                        else
                            ImGui::Text("%d <gone>", dc.EntityID);
                    }
                    else
                    {
                        ImGui::Text("(none)");
                    }

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%u", dc.IndexCount / 3);
                }
                ImGui::EndTable();
            }
            ImGui::End();
        }

        // Pre-Z / Shadow / SSAO 合并面板
        {
            ImGui::Begin("Render Passes", nullptr, ImGuiWindowFlags_NoTitleBar);

            if (ImGui::BeginTabBar("RenderPassTabs"))
            {
                // ---- Pre-Z 页签 ----
                if (ImGui::BeginTabItem("Pre-Z"))
                {
                    ImGui::TextDisabled("Prepass Depth");
                    uint32_t prepassTex = SceneRenderer::GetPrepassDepthTextureID();
                    if (prepassTex)
                        ImGui::Image((void*)(uint64_t)prepassTex, ImVec2(320, 180), ImVec2(0, 1), ImVec2(1, 0));
                    ImGui::EndTabItem();
                }

                // ---- Shadow 页签 ----
                if (ImGui::BeginTabItem("Shadow"))
                {
                    auto& s = SceneRenderer::GetShadowSettings();

                    const char* modeLabels[] = { "Off", "PCF 3x3 / Variable", "PCSS" };
                    ImGui::Combo("Mode", &s.Mode, modeLabels, IM_ARRAYSIZE(modeLabels));

                    ImGui::Separator();
                    ImGui::TextDisabled("Bias");
                    ImGui::DragFloat("Depth Bias",  &s.DepthBias,  0.0001f, 0.0f, 0.02f, "%.5f");
                    ImGui::DragFloat("Normal Bias", &s.NormalBias, 0.05f,   0.0f, 10.0f, "%.2f");

                    ImGui::Separator();
                    ImGui::TextDisabled("Softness");
                    ImGui::DragFloat("PCF Radius",      &s.PcfRadius,  0.1f, 1.0f, 8.0f,   "%.2f texel");
                    ImGui::DragFloat("PCSS Light Size", &s.LightSize,  0.1f, 0.0f, 32.0f,  "%.2f");

                    ImGui::Separator();
                    ImGui::TextDisabled("Coverage");
                    ImGui::DragFloat("Ortho Half Size", &s.OrthoHalfSize, 0.5f, 1.0f, 200.0f, "%.1f world");

                    if (ImGui::Button("Reset"))
                        s = ShadowSettings{};

                    ImGui::Separator();
                    ImGui::TextDisabled("Shadow Map Depth");
                    uint32_t shadowTex = SceneRenderer::GetShadowMapTextureID();
                    if (shadowTex)
                        ImGui::Image((void*)(uint64_t)shadowTex, ImVec2(320, 320), ImVec2(0, 1), ImVec2(1, 0));

                    ImGui::EndTabItem();
                }

                // ---- SSAO 页签 ----
                if (ImGui::BeginTabItem("SSAO"))
                {
                    bool& enabled = SceneRenderer::GetSSAOEnabled();
                    ImGui::Checkbox("Enabled", &enabled);
                    auto& ss = SceneRenderer::GetSSAOSettings();
                    ImGui::DragFloat("Radius", &ss.Radius, 0.01f, 0.01f, 5.0f, "%.3f");
                    ImGui::DragFloat("Bias",   &ss.Bias,   0.001f, 0.0f, 0.2f, "%.4f");
                    ImGui::DragFloat("Power",  &ss.Power,  0.05f,  0.1f, 8.0f, "%.2f");
                    ImGui::SliderInt("Kernel Size", &ss.KernelSize, 4, 64);
                    if (ImGui::Button("Reset"))
                        ss = SSAOSettings{};

                    ImGui::Separator();
                    ImGui::TextDisabled("SSAO Output (R = visibility)");
                    uint32_t ssaoTex = SceneRenderer::GetSSAOTextureID();
                    if (ssaoTex)
                        ImGui::Image((void*)(uint64_t)ssaoTex, ImVec2(320, 180), ImVec2(0, 1), ImVec2(1, 0));

                    ImGui::TextDisabled("View-space Normal (from Z-Prepass)");
                    uint32_t nTex = SceneRenderer::GetViewNormalTextureID();
                    if (nTex)
                        ImGui::Image((void*)(uint64_t)nTex, ImVec2(320, 180), ImVec2(0, 1), ImVec2(1, 0));

                    ImGui::EndTabItem();
                }

                // ---- RSM 页签 ----
                // RSM 先从方向光的位置画一张图，记下受光表面的位置、朝向和反射光颜色。
                // 再从图里选一些点，估算它们反射的光有多少能照到眼前的表面。
                if (ImGui::BeginTabItem("RSM"))
                {
                    auto& rs = SceneRenderer::GetRSMSettings();

                    ImGui::Checkbox("Enabled", &rs.Enabled);

                    ImGui::Separator();
                    ImGui::TextUnformatted("RSM Parameters");

                    // 每个像素要从光源图里取多少个点。取点越多，噪点通常越少，计算也越久。
                    ImGui::SliderInt("Num Samples", &rs.NumSamples, 16, 128);
                    ImGui::SameLine(); ImGui::TextDisabled("(VPLs per pixel)");

                    // 只调整反射光的亮度。取 1 时保留算法算出的值。
                    ImGui::DragFloat("Intensity",     &rs.Intensity,     0.2f, 0.0f, 50.0f, "%.2f");
                    ImGui::SameLine(); ImGui::TextDisabled("(1-bounce strength)");

                    // 半径用光源图的宽度表示，0.1 表示图宽的十分之一。
                    ImGui::DragFloat("Sample Radius", &rs.SampleRadius, 0.001f, 0.005f, 0.5f, "%.3f");
                    ImGui::SameLine(); ImGui::TextDisabled("(UV-space radius)");

                    if (ImGui::Button("Reset"))
                        rs = RSMSettings{};

                    // 0 只显示这次算出的间接光。0.9 表示上一帧的颜色占九成。
                    ImGui::Separator();
                    ImGui::TextUnformatted("TAA Temporal Accumulation");
                    ImGui::SliderFloat("Blend Alpha", &SceneRenderer::GetRSMBlendAlpha(), 0.0f, 0.99f, "%.2f");
                    ImGui::SameLine(); ImGui::TextDisabled("(0=no TAA, 0.9=strong smoothing)");

                    ImGui::Separator();
                    ImGui::TextDisabled("RSM Flux  (reflected light per unit projected area)");
                    uint32_t fluxTex = SceneRenderer::GetRSMFluxTextureID();
                    if (fluxTex)
                        ImGui::Image((void*)(uint64_t)fluxTex, ImVec2(256, 256), ImVec2(0, 1), ImVec2(1, 0));

                    ImGui::TextDisabled("RSM Normal  (world-space, packed [0,1])");
                    uint32_t normalTex = SceneRenderer::GetRSMNormalTextureID();
                    if (normalTex)
                        ImGui::Image((void*)(uint64_t)normalTex, ImVec2(256, 256), ImVec2(0, 1), ImVec2(1, 0));

                    ImGui::TextDisabled("RSM Distance  (light-view depth)");
                    uint32_t distTex = SceneRenderer::GetRSMDistanceTextureID();
                    if (distTex)
                        ImGui::Image((void*)(uint64_t)distTex, ImVec2(256, 256), ImVec2(0, 1), ImVec2(1, 0));

                    ImGui::TextDisabled("RSM Resolved  (TAA accumulated indirect)");
                    uint32_t resolvedTex = SceneRenderer::GetRSMResolvedTextureID();
                    if (resolvedTex)
                        ImGui::Image((void*)(uint64_t)resolvedTex, ImVec2(256, 256), ImVec2(0, 1), ImVec2(1, 0));

                    ImGui::EndTabItem();
                }

                // ---- Bloom 页签 ----
                if (ImGui::BeginTabItem("Bloom"))
                {
                    auto& bs = SceneRenderer::GetBloomSettings();

                    ImGui::Checkbox("Enabled", &bs.Enabled);

                    ImGui::Separator();
                    ImGui::TextUnformatted("Bloom Parameters");

                    // 调高后，只有更亮的表面才会产生明显的光晕。传给着色器的值最低为 0.5。
                    ImGui::DragFloat("Threshold",  &bs.Threshold,  0.05f, 0.0f, 5.0f,  "%.2f");
                    ImGui::SameLine(); ImGui::TextDisabled("(lum > this triggers bloom)");

                    // 合成时控制光晕的亮度。超过 1 按 1 计算。
                    ImGui::DragFloat("Intensity",  &bs.Intensity,  0.05f, 0.0f, 5.0f,  "%.2f");

                    // 放大光晕图时会取周围十二处的颜色。半径越大，取色位置离中心越远。
                    ImGui::DragFloat("Radius",     &bs.Radius,     0.05f, 0.1f, 10.0f, "%.2f");
                    ImGui::SameLine(); ImGui::TextDisabled("(Poisson disk radius per upsample)");

                    // 每多用一层，就多取一张更小的光晕图，放大叠回去后光晕会更宽。
                    ImGui::SliderInt("Mip Levels", &bs.MipLevels,  2, 6);
                    ImGui::SameLine(); ImGui::TextDisabled("(more = larger bloom radius)");

                    if (ImGui::Button("Reset"))
                        bs = BloomSettings{};

                    ImGui::Separator();
                    // 这里显示各层叠加后的光晕，下一步才把它加到场景颜色上。
                    ImGui::TextDisabled("Bloom Mip[0] (actual bloom buffer)");
                    uint32_t bloomTex = SceneRenderer::GetBloomTextureID();
                    if (bloomTex)
                        ImGui::Image((void*)(uint64_t)bloomTex, ImVec2(320, 180), ImVec2(0, 1), ImVec2(1, 0));

                    ImGui::EndTabItem();
                }

                // ---- ToneMapping 页签 ----
                if (ImGui::BeginTabItem("ToneMapping"))
                {
                    auto& ts = SceneRenderer::GetToneMapSettings();

                    ImGui::Separator();
                    ImGui::TextUnformatted("ACES Filmic");

                    ImGui::DragFloat("Exposure", &ts.Exposure, 0.05f, 0.01f, 10.0f, "%.2f");
                    ImGui::SameLine(); ImGui::TextDisabled("(HDR multiplier before ACES)");

                    if (ImGui::Button("Reset"))
                        ts = ToneMapSettings{};

                    // ---- ACES 曲线预览 ----
                    // 横轴是场景原本的亮度，纵轴是经过曝光、ACES 和伽马校正后送到屏幕的值。
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::TextUnformatted("Tone Curve  ( X = linear HDR [0..4],  Y = display )");

                    ImVec2      canvasPos = ImGui::GetCursorScreenPos();
                    const float canvasW   = ImGui::GetContentRegionAvail().x;
                    const float canvasH   = 140.0f;
                    ImDrawList* dl        = ImGui::GetWindowDrawList();

                    // 背景 & 边框
                    dl->AddRectFilled(canvasPos,
                        { canvasPos.x + canvasW, canvasPos.y + canvasH },
                        IM_COL32(25, 25, 25, 255));
                    dl->AddRect(canvasPos,
                        { canvasPos.x + canvasW, canvasPos.y + canvasH },
                        IM_COL32(70, 70, 70, 255));

                    // 画布的对角线，方便对照曲线在哪一段变平。
                    dl->AddLine({ canvasPos.x,         canvasPos.y + canvasH },
                                 { canvasPos.x + canvasW, canvasPos.y },
                                 IM_COL32(55, 55, 55, 200));

                    // 中间水平网格线（output = 0.5）
                    dl->AddLine({ canvasPos.x,         canvasPos.y + canvasH * 0.5f },
                                 { canvasPos.x + canvasW, canvasPos.y + canvasH * 0.5f },
                                 IM_COL32(45, 45, 45, 180));

                    // 沿用 PostProcess_Composite.glsl 中 Narkowicz 的 ACES 拟合公式和系数。
                    auto acesF = [](float x) -> float {
                        const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
                        float v = (x * (a * x + b)) / (x * (c * x + d) + e);
                        return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
                    };

                    const float xMax     = 4.0f;  // 横轴画出曝光处理前 0 到 4 的亮度
                    const int   STEPS    = 200;
                    const float exposure = ts.Exposure;
                    ImVec2      prevPt{};
                    for (int i = 0; i <= STEPS; ++i)
                    {
                        float t = (float)i / (float)STEPS;
                        float x = t * xMax;                      // 原始 HDR 值
                        float y = acesF(x * exposure);           // 曝光后送入 ACES
                        y = powf(y, 1.0f / 2.2f);               // Gamma 2.2，与 shader 一致
                        ImVec2 pt{ canvasPos.x + t * canvasW,
                                   canvasPos.y + (1.0f - y) * canvasH };
                        if (i > 0)
                            dl->AddLine(prevPt, pt, IM_COL32(255, 200, 50, 230), 2.0f);
                        prevPt = pt;
                    }

                    // 标出输入亮度等于 1 的位置。
                    {
                        float mx = (1.0f / xMax) * canvasW;
                        dl->AddLine({ canvasPos.x + mx, canvasPos.y },
                                     { canvasPos.x + mx, canvasPos.y + canvasH },
                                     IM_COL32(100, 180, 255, 100));
                        dl->AddText({ canvasPos.x + mx + 3.0f, canvasPos.y + 3.0f },
                                     IM_COL32(100, 180, 255, 200), "1.0");
                    }

                    // 让 ImGui 占据画布高度，避免后续控件重叠
                    ImGui::Dummy({ canvasW, canvasH });

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::End();
        }

        // 左侧面板
        m_SceneHierachyPanel.OnImguiRender();
        m_ContentBrowserPanel.OnImGuiRender();
        m_DevContext.Draw();

        // Viewport 面板
        const bool visible = ImGui::Begin("Viewport");

        m_ViewportFocused = ImGui::IsWindowFocused();
        const bool game = m_SceneState == SceneState::Play;
        if (visible && game) m_GameViewport.DrawResolution();
        ImVec2 available = ImGui::GetContentRegionAvail();
        if (!visible || available.x < 1.f || available.y < 1.f)
        {
            m_ViewportBounds[0] = m_ViewportBounds[1] = {};
            m_ViewportHovered = false;
            Application::Get().GetImGuiLayer()->BlockEvents(true);
            ImGui::End(); // Viewport
            ImGui::End(); // DockSpace
            return;
        }
        const GameViewport::Layout layout = m_GameViewport.Fit(available, ImGui::GetIO().DisplayFramebufferScale, game);
        if (m_ViewportSize != layout.Pixels)
        {
            m_ViewportSize = layout.Pixels;
            m_EditorCamera.SetViewportSize(float(m_ViewportSize.x), float(m_ViewportSize.y));
            const glm::uvec2 gameSize = m_GameViewport.Resolution(layout.Pixels);
            m_ActiveScene->OnViewportResize(gameSize.x, gameSize.y);
            m_Framebuffer->Resize(m_ViewportSize.x, m_ViewportSize.y);
        }
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos({origin.x + layout.Offset.x, origin.y + layout.Offset.y});

        // 面板显示后处理画好的图。曝光和色调映射保留了亮部的明暗差别。
        uint32_t textureID = SceneRenderer::GetFinalColorTextureID();
        if (textureID != 0)
        {
            ImGui::Image((void*)(uint64_t)textureID,
                layout.Size,
                ImVec2(0, 1), ImVec2(1, 0));
        }
        else
        {
            // 第一张图还没画好，先给场景面板留出位置。
            ImGui::Dummy(layout.Size);
        }
        ImVec2 topLeft = ImGui::GetItemRectMin(), bottomRight = ImGui::GetItemRectMax();
        m_ViewportBounds[0] = {topLeft.x, topLeft.y};
        m_ViewportBounds[1] = {bottomRight.x, bottomRight.y};
        m_ViewportHovered = ImGui::IsItemHovered();
        Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused || !m_ViewportHovered);

        // 拖拽 .redflag 场景文件到 Viewport 时直接打开
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_FILE"))
            {
                const char* droppedPath = (const char*)payload->Data;
                if (droppedPath)
                {
                    if (m_SceneState != SceneState::Edit)
                        OnSceneStop();

                    m_HoveredEntity = {};

                    m_EditorScene = CreateRef<Scene>();
                    const glm::uvec2 gameSize = m_GameViewport.Resolution(m_ViewportSize);
                    m_EditorScene->OnViewportResize(gameSize.x, gameSize.y);
                    m_ActiveScene = m_EditorScene;
                    m_SceneHierachyPanel.SetContext(m_EditorScene);

                    SceneSerializer serializer(m_EditorScene);
                    if (serializer.Deserialize(std::string(droppedPath)))
                    {
                        m_SceneFilePath = std::string(droppedPath);
                        m_SceneHierachyPanel.OpenFieldNotes(m_SceneFilePath);
                        RF_CORE_INFO("Successfully opened scene: {0}", m_SceneFilePath);
                    }
                    else
                    {
                        m_SceneFilePath.clear();
                        m_SceneHierachyPanel.OpenFieldNotes({});
                        RF_CORE_ERROR("Failed to open scene: {0}", droppedPath);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Gizmo（平移/旋转/缩放，仅在 Edit / Simulate 下可用）
        Entity selectedEntity = m_SceneHierachyPanel.GetSelectedEntity();
        const bool canUseGizmo = m_SceneState != SceneState::Play;
        if (canUseGizmo && selectedEntity && m_GizmoType != -1)
        {
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
                m_ViewportBounds[1].x - m_ViewportBounds[0].x,
                m_ViewportBounds[1].y - m_ViewportBounds[0].y);

            const glm::mat4& cameraProjection = m_EditorCamera.GetProjection();
            glm::mat4        cameraView       = m_EditorCamera.GetViewMatrix();

            auto& tc = selectedEntity.GetComponent<TransformComponent>();
            glm::mat4 transform = tc.GetTransform();

            bool  snap      = Input::IsKeyPressed(RF_KEY_LEFT_CONTROL);
            float snapValue = (m_GizmoType == ImGuizmo::OPERATION::ROTATE) ? 45.0f : 0.5f;
            float snapValues[3] = { snapValue, snapValue, snapValue };

            ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                (ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL,
                glm::value_ptr(transform),
                nullptr, snap ? snapValues : nullptr);

            if (ImGuizmo::IsUsing())
            {
                glm::vec3 translation, rotation, scale;
                Math::DecomposeTransform(transform, translation, rotation, scale);

                glm::vec3 deltaRotation = rotation - tc.Rotation;
                tc.Translation  = translation;
                tc.Rotation    += deltaRotation;
                tc.Scale        = scale;
            }
        }

        ImGui::End(); // Viewport
        ImGui::End(); // DockSpace
    }

    void ForgeLayer::OnEvent(Event& event)
    {
        m_CameraController.OnEvent(event);
        m_EditorCamera.OnEvent(event);

        EventDispatcher dispatcher(event);
        dispatcher.Dispatch<KeyPressedEvent>(RF_BIND_EVENT_FN(ForgeLayer::OnKeyPressed));
        dispatcher.Dispatch<MouseButtonPressedEvent>(RF_BIND_EVENT_FN(ForgeLayer::OnMouseButtonPressed));
    }

    bool ForgeLayer::OnKeyPressed(KeyPressedEvent& e)
    {
        if (e.GetRepeatCount() > 0)
            return false;

        bool control = Input::IsKeyPressed(RF_KEY_LEFT_CONTROL) ||
                       Input::IsKeyPressed(RF_KEY_RIGHT_CONTROL);
        bool shift   = Input::IsKeyPressed(RF_KEY_LEFT_SHIFT)   ||
                       Input::IsKeyPressed(RF_KEY_RIGHT_SHIFT);

        switch (e.GetKeyCode())
        {
        case RF_KEY_N: if (control)         NewScene();    break;
        case RF_KEY_O: if (control)         OpenScene();   break;
        case RF_KEY_S:
            if (control && shift) SaveSceneAs();
            else if (control)     SaveScene();
            break;
        case RF_KEY_D:
            if (control) OnDuplicateEntity();
            break;

        // Gizmo 切换（Q=无 W=移动 R=旋转 E=缩放）
        case RF_KEY_Q: m_GizmoType = -1;                             break;
        case RF_KEY_W: m_GizmoType = ImGuizmo::OPERATION::TRANSLATE; break;
        case RF_KEY_R: m_GizmoType = ImGuizmo::OPERATION::ROTATE;    break;
        case RF_KEY_E: m_GizmoType = ImGuizmo::OPERATION::SCALE;     break;
        }

        return false;
    }

    bool ForgeLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (e.GetMouseButton() == RF_MOUSE_BUTTON_LEFT)
        {
            // 播放时显示的是场景副本，按停止就会销毁。因此播放期间的点击不会把副本里的物体交给属性面板。
            if (m_ViewportHovered && !ImGuizmo::IsOver() &&
                !Input::IsKeyPressed(RF_KEY_LEFT_ALT) &&
                m_SceneState != SceneState::Play)
            {
                m_SceneHierachyPanel.SetSelectedEntity(m_HoveredEntity);
            }
        }
        return false;
    }

    // ----------
    // 场景状态切换
    //
    //  Edit ──[Play 按钮]──────► Play
    //  Play ──[Stop 按钮]──────► Edit
    //  Edit ──[Simulate 按钮]──► Simulate
    //  Simulate ──[Stop 按钮]──► Edit
    //
    //  Play     : 复制副本 → OnRuntimeStart（脚本+物理）→ 场景主相机渲染
    //  Simulate : 复制副本 → OnSimulationStart（仅物理）→ EditorCamera 渲染
    //  Stop     : 停止物理/脚本，丢弃副本，恢复 m_EditorScene
    // ----------

    void ForgeLayer::OnScenePlay()
    {
        if (m_SceneState != SceneState::Edit)
            return;

        m_ActiveScene = Scene::Copy(m_EditorScene); // 复制，保护编辑数据
        m_ActiveScene->OnRuntimeStart();            // 启动脚本 + 物理
        m_SceneState  = SceneState::Play;
    }

    void ForgeLayer::OnSceneStop()
    {
        m_ActiveScene->OnRuntimeStop();
        m_HoveredEntity = {};           // 鼠标指着的是副本里的物体，切回编辑场景前先清掉。
        m_ActiveScene = m_EditorScene;  // 恢复编辑场景
        m_SceneState  = SceneState::Edit;
    }

    void ForgeLayer::OnSceneSimulate()
    {
        if (m_SceneState != SceneState::Edit)
            return;

        m_ActiveScene = Scene::Copy(m_EditorScene); // 复制，保护编辑数据
        m_ActiveScene->OnSimulationStart();         // 仅启动物理
        m_SceneState  = SceneState::Simulate;
    }

    void ForgeLayer::OnSceneSimulateStop()
    {
        m_ActiveScene->OnSimulationStop(); // 停止物理
        m_HoveredEntity = {};              // 同样清掉鼠标指着的模拟场景物体。
        m_ActiveScene = m_EditorScene;     // 恢复编辑场景
        m_SceneState  = SceneState::Edit;
    }

    void ForgeLayer::OnDuplicateEntity()
    {
        // 只允许在编辑模式下复制实体
        if (m_SceneState != SceneState::Edit)
            return;

        Entity selectedEntity = m_SceneHierachyPanel.GetSelectedEntity();
        if (selectedEntity)
            m_EditorScene->DuplicateEntity(selectedEntity);
    }

    static bool SaveSceneFile(const Ref<Scene>& scene, const std::string& path)
    {
        try
        {
            SceneSerializer(scene).Serialize(path);
            RF_CORE_INFO("Scene saved: {0}", path);
            return true;
        }
        catch (const std::exception& error)
        {
            RF_CORE_ERROR("Could not save scene: {0}", error.what());
            return false;
        }
    }

    void ForgeLayer::SaveScene()
    {
        if (!m_SceneFilePath.empty())
        {
            SaveSceneFile(m_EditorScene, m_SceneFilePath);
        }
        else
        {
            SaveSceneAs(); // 首次保存时弹出路径选择
        }
    }

    void ForgeLayer::SaveSceneAs()
    {
        std::string filepath = FileDialogs::SaveFile("Redflag Scene (*.redflag)\0*.redflag\0");
        if (!filepath.empty())
        {
            if (filepath.find(".redflag") == std::string::npos)
                filepath += ".redflag";

            if (SaveSceneFile(m_ActiveScene, filepath))
            {
                m_SceneHierachyPanel.SaveFieldNotesAs(filepath);
                m_SceneFilePath = filepath;
            }
        }
    }

    void ForgeLayer::OpenScene()
    {
        // 如果当前在运行/模拟，先停止再打开
        if (m_SceneState != SceneState::Edit)
            OnSceneStop();

        std::string filepath = FileDialogs::OpenFile("Redflag Scene (*.redflag)\0*.redflag\0");
        if (!filepath.empty())
        {
            m_HoveredEntity = {};

            m_EditorScene = CreateRef<Scene>();
            const glm::uvec2 gameSize = m_GameViewport.Resolution(m_ViewportSize);
            m_EditorScene->OnViewportResize(gameSize.x, gameSize.y);

            SceneSerializer serializer(m_EditorScene);
            if (serializer.Deserialize(filepath))
            {
                m_ActiveScene   = m_EditorScene;
                m_SceneFilePath = filepath;
                m_SceneHierachyPanel.OpenFieldNotes(filepath);
                m_SceneHierachyPanel.SetContext(m_EditorScene);
                RF_CORE_INFO("Successfully opened scene: {0}", filepath);
            }
            else
            {
                // 场景文件没能读完，层级面板显示这次新建的场景。
                m_ActiveScene = m_EditorScene;
                m_SceneHierachyPanel.SetContext(m_EditorScene);
                m_SceneFilePath.clear();
                m_SceneHierachyPanel.OpenFieldNotes({});
                RF_CORE_ERROR("Failed to open scene: {0}", filepath);
            }
        }
    }

    void ForgeLayer::NewScene()
    {
        RF_CORE_INFO("Creating new scene...");

        if (m_SceneState != SceneState::Edit)
            OnSceneStop();

        m_HoveredEntity = {};

        m_EditorScene = CreateRef<Scene>();
        m_EditorScene->SetEnvironment(DefaultSky);
        const glm::uvec2 gameSize = m_GameViewport.Resolution(m_ViewportSize);
        m_EditorScene->OnViewportResize(gameSize.x, gameSize.y);
        m_ActiveScene = m_EditorScene;
        m_SceneHierachyPanel.SetContext(m_EditorScene);
        m_SceneFilePath.clear();
        m_SceneHierachyPanel.OpenFieldNotes({});

        m_Framebuffer->Bind();
        m_Framebuffer->ClearAttachment(1, -1);
        m_Framebuffer->ClearAttachment(2, 0);
        RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        RenderCommand::Clear();
        m_Framebuffer->Unbind();

        RF_CORE_INFO("New empty scene created");
    }

} // namespace Redflag
