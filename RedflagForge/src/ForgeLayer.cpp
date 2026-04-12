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

    ForgeLayer::ForgeLayer()
        : Layer("ForgeLayer"), m_CameraController(1600.0f / 900.0f, true)
    {
    }

    void ForgeLayer::OnAttach()
    {
        RF_PROFILE_FUNCTION();

        // ============== ����������Դ =========================
        m_RedCloudTexture = Texture2D::Create("assets/textures/RedCloud.png");
        m_CheckerboardTexture = Texture2D::Create("assets/textures/Checkerboard.png");
        m_IconPlay = Texture2D::Create("assets/icons/PlayButton.png");
        m_IconStop = Texture2D::Create("assets/icons/StopButton.png");

        // ============== ���������ʼλ�� =========================
        m_CameraController.SetZoomLevel(6.f);

        // =============== ����֡������ ===============================
        FramebufferSpecification fbSpec;
        fbSpec.Attachments = { FramebufferTextureFormat::RGBA8,
                               FramebufferTextureFormat::RED_INTEGER,
                               FramebufferTextureFormat::Depth };
        fbSpec.Width = 1600;
        fbSpec.Height = 900;
        m_Framebuffer = Framebuffer::Create(fbSpec);

        // =============== 3D test: create cube mesh ===============================
        m_CubeMesh = Mesh::CreateCube();

        // =============== �������� ===============================
        m_EditorScene = CreateRef<Scene>();
        m_EditorScene->OnViewportResize(1600, 900);
        m_ActiveScene = m_EditorScene;
        m_SceneHierachyPanel.SetContext(m_EditorScene);

        // =============== ���� EditorCamera ===============================
        m_EditorCamera = EditorCamera(30.f, 1.788f, 0.1f, 1000.f);
    }

    void ForgeLayer::OnDetach()
    {
        RF_PROFILE_FUNCTION();
    }

    void ForgeLayer::OnUpdate(Timestep ts)
    {
        RF_PROFILE_FUNCTION();

        // Camera ����
        if (m_ViewportFocused)
        {
            m_CameraController.OnUpdate(ts);
            m_EditorCamera.OnUpdate(ts);
        }

        Renderer2D::ResetStats();

        // ��Ⱦ��ʼ
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

        // --- ���ʰȡ�߼� ---
        auto [mx, my] = ImGui::GetMousePos();
        mx -= m_ViewportBounds[0].x;
        my -= m_ViewportBounds[0].y;
        glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
        my = viewportSize.y - my; // ��ת Y������ OpenGL ����ϵ

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

        // ======================================================
        // DOCKING ģʽ
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
        // MenuBar����� File �˵� �� �м� Play/Stop ��ť
        // ======================================================
        if (ImGui::BeginMenuBar())
        {
            // ---------- ��ࣺFile �˵� ----------
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
            // ���Ű�ť
            {
                Ref<Texture2D> icon = (m_SceneState == SceneState::Edit) ? m_IconPlay : m_IconStop;
                const char* label = (m_SceneState == SceneState::Edit) ? "Play" : "Stop";
                float requestedIconSize = 24.0f;
                float spacing = 18.0f; // <- �������ֵ�Էֿ���ť������

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

                // ����ͼ��
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

                // ��ȷ�������֣�ȷ�����ص���
                ImGui::SetCursorPosX(startX + iconSize + spacing);
                ImGui::SetCursorPosY(cursorStartY + offsetY + (iconSize - textSize.y) * 0.5f);
                ImGui::TextUnformatted(label);

                if (ImGui::IsItemHovered()) ImGui::SetTooltip(label);
            }
            ImGui::EndMenuBar();
        }

        // ======================================================
        // Profile ���
        // ======================================================
        {
            RF_PROFILE_SCOPE("Stats");

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 6));
            ImGui::Begin("Profile");

            // ── 颜色定义 ──
            const ImVec4 colRed       = ImVec4(0.80f, 0.08f, 0.08f, 1.0f);
            const ImVec4 colWhite     = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
            const ImVec4 colDimWhite  = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
            const ImVec4 colHighlight = ImVec4(0.85f, 0.15f, 0.15f, 1.0f);
            const ImVec4 colSeparator = ImVec4(0.70f, 0.10f, 0.10f, 0.70f);

            // ── 实体信息 ──
            Entity selectedEntity = m_SceneHierachyPanel.GetSelectedEntity();

            std::string hoveredName  = "None";
            if (m_HoveredEntity)
                hoveredName = m_HoveredEntity.GetComponent<TagComponent>().Tag;

            std::string selectedName = "None";
            if (selectedEntity)
                selectedName = selectedEntity.GetComponent<TagComponent>().Tag;

            // Hovered
            ImGui::PushStyleColor(ImGuiCol_Text, colDimWhite);
            ImGui::Text("Hovered ");
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 0);
            ImGui::PushStyleColor(ImGuiCol_Text, m_HoveredEntity ? colHighlight : colDimWhite);
            ImGui::Text("%s", hoveredName.c_str());
            ImGui::PopStyleColor();

            // Selected
            ImGui::PushStyleColor(ImGuiCol_Text, colDimWhite);
            ImGui::Text("Selected");
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 4);
            ImGui::PushStyleColor(ImGuiCol_Text, selectedEntity ? colWhite : colDimWhite);
            ImGui::Text("%s", selectedName.c_str());
            ImGui::PopStyleColor();

            // ── 分隔线 ──
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Separator, colSeparator);
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Spacing();

            // ── 渲染统计 ──
            auto stats2D = Renderer2D::GetStats();
            auto stats3D = Renderer3D::GetStats();
            uint32_t totalDrawCalls = stats2D.DrawCalls + stats3D.DrawCalls;
            uint32_t totalVertices  = stats2D.GetTotalVertexCount() + stats3D.VertexCount;
            uint32_t totalIndices   = stats2D.GetTotalIndexCount()  + stats3D.IndexCount;

            // 标题
            ImGui::PushStyleColor(ImGuiCol_Text, colRed);
            ImGui::Text("Render Stats");
            ImGui::PopStyleColor();
            ImGui::Spacing();

            // 总览行
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

            // 2D / 3D 分列
            float halfW = ImGui::GetContentRegionAvail().x * 0.5f;

            // ─── 2D 列 ───
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

            // ─── 3D 列 ───
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

        // ======================================================
        // Scene Hierarchy & Content Browser
        // ======================================================
        m_SceneHierachyPanel.OnImguiRender();
        m_ContentBrowserPanel.OnImGuiRender();

        // ======================================================
        // Viewport ����
        // ======================================================
        ImGui::Begin("Viewport");

        // 1. �����ӿڱ߽�
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

        // 2. �Զ� Resize
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (m_ViewportSize != *((glm::vec2*)&viewportPanelSize))
        {
            m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };
            m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
            m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        }

        // 3. ���Ƴ�������
        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID(0);
        ImGui::Image((void*)(uint64_t)textureID,
            ImVec2(m_ViewportSize.x, m_ViewportSize.y),
            ImVec2(0, 1), ImVec2(1, 0));

        // 4. ��ק���س����ļ�
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_FILE"))
            {
                const char* droppedPath = (const char*)payload->Data;
                if (droppedPath)
                {
                    // ��ֹͣ����
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
        case RF_KEY_D:
            if (control)
            OnDuplicateEntity();
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
        if (m_SceneState != SceneState::Edit)
            return;

        m_ActiveScene = Scene::Copy(m_EditorScene);
        m_ActiveScene->OnRuntimeStart();
        m_SceneState = SceneState::Play;
    }

    void ForgeLayer::OnSceneStop()
    {
        m_SceneState = SceneState::Edit;
        m_ActiveScene->OnRuntimeStop();

        // �лر༭���������������ü��������Զ����٣�
        m_ActiveScene = m_EditorScene;
        m_SceneState = SceneState::Edit;
    }

    void ForgeLayer::OnDuplicateEntity()
    {
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
        // 1. ������ڲ��ţ���ֹͣ
        if (m_SceneState != SceneState::Edit)
            OnSceneStop();

        // 2. �����ļ�ѡ��Ի���
        std::string filepath = FileDialogs::OpenFile("Redflag Scene (*.redflag)\0*.redflag\0");
        if (!filepath.empty())
        {
            // 3. �����ͣʵ��
            m_HoveredEntity = {};

            // 4. �����µı༭������
            m_EditorScene = CreateRef<Scene>();
            m_EditorScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

            // 5. ���ļ��������ݵ��༭������
            SceneSerializer serializer(m_EditorScene);
            if (serializer.Deserialize(filepath))
            {
                // 6. ���سɹ�������״̬
                m_ActiveScene = m_EditorScene;
                m_SceneFilePath = filepath;

                // 7. ���� Hierarchy ���
                m_SceneHierachyPanel.SetContext(m_EditorScene);

                RF_CORE_INFO("Successfully opened scene: {0}", filepath);
            }
            else
            {
                // 8. ����ʧ�ܣ��ָ��ճ���
                m_EditorScene = CreateRef<Scene>();
                m_EditorScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
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