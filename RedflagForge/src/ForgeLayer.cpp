#include "rfpch.h"

#include"Redflag.h"
#include "ForgeLayer.h"

#include "Platform/OpenGL/OpenGLShader.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Redflag/Renderer/Shader.h"
#include "Redflag/Renderer/RenderCommand.h"
#include"Redflag/Scene/CameraControllerScript.h"
#include "Redflag/Scene/SceneSerializer.h"
#include "Redflag/Utils/PlatformUtils.h"

#include"Redflag/Renderer/EditorCamera.h"

#include"ImGuizmo.h"
#include"Redflag/Math/Math.h"


namespace Redflag {

    // =================================================================
    // 编辑器 Gizmo 绘制辅助函数
    // 在 Renderer2D::BeginScene / EndScene 之间调用
    // =================================================================

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

    static void DrawPointLightGizmo(const glm::vec3& pos, float radius, const glm::vec4& color)
    {
        constexpr int segments = 16;
        for (int i = 0; i < segments; i++)
        {
            float a0 = glm::two_pi<float>() * i / segments;
            float a1 = glm::two_pi<float>() * (i + 1) / segments;
            float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
            Renderer2D::DrawLine(pos + glm::vec3(radius * c0, radius * s0, 0.0f),
                                 pos + glm::vec3(radius * c1, radius * s1, 0.0f), color);
            Renderer2D::DrawLine(pos + glm::vec3(radius * c0, 0.0f, radius * s0),
                                 pos + glm::vec3(radius * c1, 0.0f, radius * s1), color);
            Renderer2D::DrawLine(pos + glm::vec3(0.0f, radius * c0, radius * s0),
                                 pos + glm::vec3(0.0f, radius * c1, radius * s1), color);
        }

        float rayLen = radius * 0.3f;
        glm::vec4 rayColor = color * glm::vec4(1.0f, 1.0f, 1.0f, 0.6f);
        Renderer2D::DrawLine(pos, pos + glm::vec3( rayLen, 0, 0), rayColor);
        Renderer2D::DrawLine(pos, pos + glm::vec3(-rayLen, 0, 0), rayColor);
        Renderer2D::DrawLine(pos, pos + glm::vec3(0,  rayLen, 0), rayColor);
        Renderer2D::DrawLine(pos, pos + glm::vec3(0, -rayLen, 0), rayColor);
        Renderer2D::DrawLine(pos, pos + glm::vec3(0, 0,  rayLen), rayColor);
        Renderer2D::DrawLine(pos, pos + glm::vec3(0, 0, -rayLen), rayColor);
    }

    ForgeLayer::ForgeLayer()
        : Layer("ForgeLayer"), m_CameraController(1600.0f / 900.0f, true)
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

        // 创建帧缓冲
        // attachment0: RGBA8        -> 颜色输出
        // attachment1: RED_INTEGER  -> Entity ID 拾取
        // attachment2: Depth
        FramebufferSpecification fbSpec;
        fbSpec.Attachments = { FramebufferTextureFormat::RGBA8,
                               FramebufferTextureFormat::RED_INTEGER,
                               FramebufferTextureFormat::Depth };
        fbSpec.Width  = 1600;
        fbSpec.Height = 900;
        m_Framebuffer = Framebuffer::Create(fbSpec);

        // 3D 测试：创建立方体 Mesh
        m_CubeMesh = Mesh::CreateCube();

        // 初始化编辑场景
        m_EditorScene = CreateRef<Scene>();
        m_EditorScene->OnViewportResize(1600, 900);
        m_ActiveScene = m_EditorScene;
        m_SceneHierachyPanel.SetContext(m_EditorScene);

        // 初始化 EditorCamera（FOV=30, Aspect=16/9, Near=0.1, Far=1000）
        m_EditorCamera = EditorCamera(30.f, 1.788f, 0.1f, 1000.f);
    }

    void ForgeLayer::OnDetach()
    {
        RF_PROFILE_FUNCTION();
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

        // 绑定帧缓冲并清屏，Entity ID 附件清为 -1（无实体）
        m_Framebuffer->Bind();
        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        RenderCommand::Clear();
        m_Framebuffer->ClearAttachment(1, -1);

        // 按场景状态分发更新逻辑
        switch (m_SceneState)
        {
            // Simulate：物理步进 + EditorCamera 渲染，不执行脚本
            case SceneState::Simulate:
            {
                m_ActiveScene->OnUpdateSimulation(ts, m_EditorCamera);
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
                m_ActiveScene->OnUpdateRuntime(ts);
                break;
            }
        }

        // Entity ID 拾取：读取鼠标悬停位置的 RED_INTEGER 附件
        auto [mx, my] = ImGui::GetMousePos();
        mx -= m_ViewportBounds[0].x;
        my -= m_ViewportBounds[0].y;
        glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
        my = viewportSize.y - my; // 翻转 Y 轴以对齐 OpenGL 坐标系

        int mouseX = (int)mx;
        int mouseY = (int)my;

        if (mouseX >= 0 && mouseY >= 0 && mouseX < (int)viewportSize.x && mouseY < (int)viewportSize.y)
        {
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

        m_Framebuffer->Unbind();
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
                ImGui::MenuItem("Physics Colliders", nullptr, &m_ShowPhysicsColliders);
                ImGui::EndMenu();
            }

            // 工具栏：Play / Simulate 两个图标按钮居中排列
            {
                float requestedIconSize = 24.0f;
                float frameH    = ImGui::GetFrameHeight();
                float maxIcon   = std::max(4.0f, frameH - 2.0f);
                float iconSize  = requestedIconSize > maxIcon ? maxIcon : requestedIconSize;
                float btnSpacing = 6.0f;

                float cursorStartX = ImGui::GetCursorPosX();
                float cursorStartY = ImGui::GetCursorPosY();
                float availW  = ImGui::GetContentRegionAvail().x;
                float startX  = cursorStartX + availW * 0.5f - (iconSize * 2 + btnSpacing) * 0.5f;
                float offsetY = (frameH - iconSize) * 0.5f;

                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 0.4f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.2f, 0.6f, 1.0f, 0.6f));

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
            ImGui::Text("Verts   %d", stats3D.VertexCount);
            ImGui::Text("Idx     %d", stats3D.IndexCount);
            ImGui::PopStyleColor();
            ImGui::EndGroup();

            ImGui::End();
            ImGui::PopStyleVar(2);
        }

        // 左侧面板
        m_SceneHierachyPanel.OnImguiRender();
        m_ContentBrowserPanel.OnImGuiRender();

        // Viewport 面板
        ImGui::Begin("Viewport");

        // 记录 Viewport 屏幕边界（用于鼠标坐标转换和 Gizmo 绘制范围）
        auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
        auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
        auto viewportOffset    = ImGui::GetWindowPos();
        m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x,
                                viewportMinRegion.y + viewportOffset.y };
        m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x,
                                viewportMaxRegion.y + viewportOffset.y };

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();
        Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused || !m_ViewportHovered);

        // Viewport 大小变化时同步帧缓冲和相机宽高比
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (m_ViewportSize != *((glm::vec2*)&viewportPanelSize))
        {
            m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };
            m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
            m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        }

        // 将帧缓冲 attachment0 的颜色输出渲染到 ImGui Image
        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID(0);
        ImGui::Image((void*)(uint64_t)textureID,
            ImVec2(m_ViewportSize.x, m_ViewportSize.y),
            ImVec2(0, 1), ImVec2(1, 0));

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
                    m_EditorScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
                    m_ActiveScene = m_EditorScene;
                    m_SceneHierachyPanel.SetContext(m_EditorScene);

                    SceneSerializer serializer(m_EditorScene);
                    if (serializer.Deserialize(std::string(droppedPath)))
                    {
                        m_SceneFilePath = std::string(droppedPath);
                        RF_CORE_INFO("Successfully opened scene: {0}", m_SceneFilePath);
                    }
                    else
                    {
                        RF_CORE_ERROR("Failed to open scene: {0}", droppedPath);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Gizmo（平移/旋转/缩放，Edit 和 Simulate 下均可用）
        Entity selectedEntity = m_SceneHierachyPanel.GetSelectedEntity();
        if (selectedEntity && m_GizmoType != -1)
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
            // 左键点击 Viewport 且未操作 Gizmo 时，选中悬停实体
            if (m_ViewportHovered && !ImGuizmo::IsOver() &&
                !Input::IsKeyPressed(RF_KEY_LEFT_ALT))
            {
                m_SceneHierachyPanel.SetSelectedEntity(m_HoveredEntity);
            }
        }
        return false;
    }

    // ==================================================================
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
    // ==================================================================

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
        m_ActiveScene->OnRuntimeStop(); // 停止物理
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

    void ForgeLayer::SaveScene()
    {
        if (!m_SceneFilePath.empty())
        {
            SceneSerializer serializer(m_EditorScene);
            serializer.Serialize(m_SceneFilePath);
            RF_CORE_INFO("Scene saved: {0}", m_SceneFilePath);
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

            SceneSerializer serializer(m_ActiveScene);
            serializer.Serialize(filepath);
            m_SceneFilePath = filepath;
            RF_CORE_INFO("Scene saved as: {0}", filepath);
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
            m_EditorScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

            SceneSerializer serializer(m_EditorScene);
            if (serializer.Deserialize(filepath))
            {
                m_ActiveScene   = m_EditorScene;
                m_SceneFilePath = filepath;
                m_SceneHierachyPanel.SetContext(m_EditorScene);
                RF_CORE_INFO("Successfully opened scene: {0}", filepath);
            }
            else
            {
                // 反序列化失败，回退到空场景
                m_ActiveScene = m_EditorScene;
                m_SceneHierachyPanel.SetContext(m_EditorScene);
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
        m_EditorScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        m_ActiveScene = m_EditorScene;
        m_SceneHierachyPanel.SetContext(m_EditorScene);
        m_SceneFilePath.clear();

        m_Framebuffer->Bind();
        m_Framebuffer->ClearAttachment(1, -1);
        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        RenderCommand::Clear();
        m_Framebuffer->Unbind();

        RF_CORE_INFO("New empty scene created");
    }

} // namespace Redflag
