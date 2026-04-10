#include "rfpch.h"

#include"Redflag.h"
#include "ForgeLayer.h"

#include "Platform/OpenGL/OpenGLShader.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Redflag/Renderer/Shader.h"
#include"Redflag/Scene/CameraControllerScript.h"
#include "Redflag/Scene/SceneSerializer.h"
#include "Redflag/Utils/PlatformUtils.h" 

#include"Redflag/Renderer/EditorCamera.h"

#include"ImGuizmo.h"
#include"Redflag/Math/Math.h"


namespace Redflag {

    ForgeLayer::ForgeLayer()
        : Layer("ForgeLayer"), m_CameraController(1600.0f / 900.0f, true)
    {
    }

    void ForgeLayer::OnAttach()
    {
        RF_PROFILE_FUNCTION();

        // ============== 加载纹理资源 =========================
        m_RedCloudTexture = Texture2D::Create("assets/textures/RedCloud.png");
        m_CheckerboardTexture = Texture2D::Create("assets/textures/Checkerboard.png");
        m_IconPlay = Texture2D::Create("assets/icons/PlayButton.png");
        m_IconStop = Texture2D::Create("assets/icons/StopButton.png");

        // ============== 配置相机初始位置 =========================
        m_CameraController.SetZoomLevel(6.f);

        // =============== 创建帧缓冲区 ===============================
        FramebufferSpecification fbSpec;
        fbSpec.Attachments = { FramebufferTextureFormat::RGBA8,
                               FramebufferTextureFormat::RED_INTEGER,
                               FramebufferTextureFormat::Depth };
        fbSpec.Width = 1600;
        fbSpec.Height = 900;
        m_Framebuffer = Framebuffer::Create(fbSpec);

        // =============== 创建场景 ===============================
        m_ActiveScene = CreateRef<Scene>();

        // =============== 创建 EditorCamera ===============================
        m_EditorCamera = EditorCamera(30.f, 1.788f, 0.1f, 1000.f);
    }

    void ForgeLayer::OnDetach()
    {
        RF_PROFILE_FUNCTION();
    }

    void ForgeLayer::OnUpdate(Timestep ts)
    {
        RF_PROFILE_FUNCTION();

        // Camera 更新
        if (m_ViewportFocused)
        {
            m_CameraController.OnUpdate(ts);
            m_EditorCamera.OnUpdate(ts);
        }

        Renderer2D::ResetStats();

        // 渲染开始
        m_Framebuffer->Bind();
        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        RenderCommand::Clear();
        m_Framebuffer->ClearAttachment(1, -1);

        switch (m_SceneState)
        {
            case SceneState::Edit:
                {
                    m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);
                    break;
                }
            case SceneState::Play:
                {
                    m_ActiveScene->OnUpdateRuntime(ts);
                    break;
                }
        }

        // --- 鼠标拾取逻辑 ---
        auto [mx, my] = ImGui::GetMousePos();
        mx -= m_ViewportBounds[0].x;
        my -= m_ViewportBounds[0].y;
        glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
        my = viewportSize.y - my; // 翻转 Y，适配 OpenGL 坐标系

        int mouseX = (int)mx;
        int mouseY = (int)my;

        if (mouseX >= 0 && mouseY >= 0 && mouseX < (int)viewportSize.x && mouseY < (int)viewportSize.y)
        {
            if (!ImGuizmo::IsUsing())
            {
                int pixelData = m_Framebuffer->ReadPixel(1, mouseX, mouseY);
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

        // ======================================================
        // DOCKING 模式
        // ======================================================
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

        // DockSpace
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f));
        }

        // ======================================================
        // MenuBar：左侧 File 菜单 ＋ 中间 Play/Stop 按钮
        // ======================================================
        if (ImGui::BeginMenuBar())
        {
            // ---------- 左侧：File 菜单 ----------
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New", "Ctrl+N"))       NewScene();
                if (ImGui::MenuItem("Open...", "Ctrl+O"))       OpenScene();
                if (ImGui::MenuItem("Save", "Ctrl+S"))       SaveScene();
                if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) SaveSceneAs();
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) Application::Get().Close();
                ImGui::EndMenu();
            }
            // 播放按钮
            {
                Ref<Texture2D> icon = (m_SceneState == SceneState::Edit) ? m_IconPlay : m_IconStop;
                const char* label = (m_SceneState == SceneState::Edit) ? "Play" : "Stop";
                float requestedIconSize = 24.0f;
                float spacing = 18.0f; // <- 增大这个值以分开按钮和文字

                ImVec2 textSize = ImGui::CalcTextSize(label);
                float frameH = ImGui::GetFrameHeight();
                float maxIcon = std::max(4.0f, frameH - 2.0f);
                float iconSize = requestedIconSize > maxIcon ? maxIcon : requestedIconSize;
                float totalWidth = iconSize + spacing + textSize.x;

                float cursorStartX = ImGui::GetCursorPosX();
                float cursorStartY = ImGui::GetCursorPosY();
                float availW = ImGui::GetContentRegionAvail().x;

                float startX = cursorStartX + availW * 0.5f - totalWidth * 0.5f;
                float offsetY = (frameH - iconSize) * 0.5f;

                // 放置图标
                ImGui::SetCursorPosX(startX);
                ImGui::SetCursorPosY(cursorStartY + offsetY);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 0.4f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.6f, 1.0f, 0.6f));
                if (ImGui::ImageButton("##playstop", (ImTextureID)(uint64_t)icon->GetRendererID(),
                    ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1),
                    ImVec4(0, 0, 0, 0)))
                {
                    if (m_SceneState == SceneState::Edit) OnScenePlay();
                    else                              OnSceneStop();
                }
                ImGui::PopStyleColor(3);

                // 精确放置文字（确保不重叠）
                ImGui::SetCursorPosX(startX + iconSize + spacing);
                ImGui::SetCursorPosY(cursorStartY + offsetY + (iconSize - textSize.y) * 0.5f);
                ImGui::TextUnformatted(label);

                if (ImGui::IsItemHovered()) ImGui::SetTooltip(label);
            }
            ImGui::EndMenuBar();
        }

        // ======================================================
        // Profile 面板
        // ======================================================
        {
            RF_PROFILE_SCOPE("Stats");

            ImGui::Begin("Profile");

            std::string name = "None";
            if (m_HoveredEntity)
                name = m_HoveredEntity.GetComponent<TagComponent>().Tag;
            ImGui::Text("Hovered Entity: %s", name.c_str());

            auto stats = Renderer2D::GetStats();
            ImGui::Text("Draw Calls: %d", stats.DrawCalls);
            ImGui::Text("Quads: %d", stats.QuadCount);
            ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
            ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

            ImGui::End();
        }

        // ======================================================
        // Scene Hierarchy & Content Browser
        // ======================================================
        m_SceneHierachyPanel.OnImguiRender();
        m_ContentBrowserPanel.OnImGuiRender();

        // ======================================================
        // Viewport 窗口
        // ======================================================
        ImGui::Begin("Viewport");

        // 1. 计算视口边界
        auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
        auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
        auto viewportOffset = ImGui::GetWindowPos();
        m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x,
                                viewportMinRegion.y + viewportOffset.y };
        m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x,
                                viewportMaxRegion.y + viewportOffset.y };

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();
        Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused || !m_ViewportHovered);

        // 2. 自动 Resize
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (m_ViewportSize != *((glm::vec2*)&viewportPanelSize))
        {
            m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };
            m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
            m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        }

        // 3. 绘制场景纹理
        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID(0);
        ImGui::Image((void*)(uint64_t)textureID,
            ImVec2(m_ViewportSize.x, m_ViewportSize.y),
            ImVec2(0, 1), ImVec2(1, 0));

        // 4. 拖拽加载场景文件
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_FILE"))
            {
                const char* droppedPath = (const char*)payload->Data;
                if (droppedPath)
                {
                    m_HoveredEntity = {};
                    m_ActiveScene = CreateRef<Scene>();
                    m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x,
                        (uint32_t)m_ViewportSize.y);
                    m_SceneHierachyPanel.SetContext(m_ActiveScene);

                    SceneSerializer serializer(m_ActiveScene);
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

        // 5. Gizmos
        Entity selectedEntity = m_SceneHierachyPanel.GetSelectedEntity();
        if (selectedEntity && m_GizmoType != -1)
        {
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
                m_ViewportBounds[1].x - m_ViewportBounds[0].x,
                m_ViewportBounds[1].y - m_ViewportBounds[0].y);

            const glm::mat4& cameraProjection = m_EditorCamera.GetProjection();
            glm::mat4        cameraView = m_EditorCamera.GetViewMatrix();

            auto& tc = selectedEntity.GetComponent<TransformComponent>();
            glm::mat4 transform = tc.GetTransform();

            bool  snap = Input::IsKeyPressed(RF_KEY_LEFT_CONTROL);
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
                tc.Translation = translation;
                tc.Rotation += deltaRotation;
                tc.Scale = scale;
            }
        }

        ImGui::End(); // End Viewport

        ImGui::End(); // End DockSpace
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
        bool shift = Input::IsKeyPressed(RF_KEY_LEFT_SHIFT) ||
            Input::IsKeyPressed(RF_KEY_RIGHT_SHIFT);

        switch (e.GetKeyCode())
        {
        case RF_KEY_N: if (control)          NewScene();    break;
        case RF_KEY_O: if (control)          OpenScene();   break;
        case RF_KEY_S:
            if (control && shift) SaveSceneAs();
            else if (control)     SaveScene();
            break;

            // Gizmos
        case RF_KEY_Q: m_GizmoType = -1;                              break;
        case RF_KEY_W: m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;  break;
        case RF_KEY_R: m_GizmoType = ImGuizmo::OPERATION::ROTATE;     break;
        case RF_KEY_E: m_GizmoType = ImGuizmo::OPERATION::SCALE;      break;
        }

        return false;
    }

    bool ForgeLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (e.GetMouseButton() == RF_MOUSE_BUTTON_LEFT)
        {
            if (m_ViewportHovered && !ImGuizmo::IsOver() &&
                !Input::IsKeyPressed(RF_KEY_LEFT_ALT))
            {
                m_SceneHierachyPanel.SetSelectedEntity(m_HoveredEntity);
            }
        }
        return false;
    }

    void ForgeLayer::OnScenePlay()
    {
        m_SceneState = SceneState::Play;
        m_ActiveScene->OnRuntimeStart();
    }

    void ForgeLayer::OnSceneStop()
    {
        m_SceneState = SceneState::Edit;
        m_ActiveScene->OnRuntimeStop();
    }

    void ForgeLayer::SaveScene()
    {
        if (!m_SceneFilePath.empty())
        {
            SceneSerializer serializer(m_ActiveScene);
            serializer.Serialize(m_SceneFilePath);
            RF_CORE_INFO("Scene saved: {0}", m_SceneFilePath);
        }
        else
        {
            SaveSceneAs();
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
        std::string filepath = FileDialogs::OpenFile("Redflag Scene (*.redflag)\0*.redflag\0");
        if (!filepath.empty())
        {
            m_HoveredEntity = {};
            m_ActiveScene = CreateRef<Scene>();
            m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x,
                (uint32_t)m_ViewportSize.y);
            m_SceneHierachyPanel.SetContext(m_ActiveScene);

            SceneSerializer serializer(m_ActiveScene);
            if (serializer.Deserialize(filepath))
            {
                m_SceneFilePath = filepath;
                RF_CORE_INFO("Successfully opened scene: {0}", filepath);
            }
        }
    }

    void ForgeLayer::NewScene()
    {
        RF_CORE_INFO("Creating new scene...");

        m_HoveredEntity = {};
        m_ActiveScene = CreateRef<Scene>();
        m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x,
            (uint32_t)m_ViewportSize.y);
        m_SceneHierachyPanel.SetContext(m_ActiveScene);
        m_SceneFilePath.clear();

        m_Framebuffer->Bind();
        m_Framebuffer->ClearAttachment(1, -1);
        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        RenderCommand::Clear();
        m_Framebuffer->Unbind();

        RF_CORE_INFO("New empty scene created");
    }

} // namespace Redflag