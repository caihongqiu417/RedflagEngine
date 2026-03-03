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

        //============== 配置相机初始位置 =========================
        m_CameraController.SetZoomLevel(6.f); // 初始缩放级别

        //===============创建帧缓冲区===============================
        FramebufferSpecification fbSpec;
        fbSpec.Attachments = { FramebufferTextureFormat::RGBA8,FramebufferTextureFormat::RED_INTEGER,FramebufferTextureFormat::Depth };
        fbSpec.Width = 1600;
        fbSpec.Height = 900;
        m_Framebuffer = Framebuffer::Create(fbSpec);


        //===============创建ECS数据===============================

        m_ActiveScene = CreateRef<Scene>();
#if 0

        m_SquareEntity = m_ActiveScene->CreateEntity("Square");
        m_SquareEntity.AddComponent<SpriteRendererComponent>(glm::vec4(0.7f, 0.1f, 0.1f, 1.0f));  // 深红色

        m_SecondSquareEntity = m_ActiveScene->CreateEntity("Square_2");
        m_SecondSquareEntity.AddComponent<SpriteRendererComponent>(glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));  // 浅灰色
        // 直接获取Transform并修改
        auto& translation = m_SecondSquareEntity.GetComponent<TransformComponent>().Translation;
        translation.x = -2.0f;     // 左移2

        m_Camera = m_ActiveScene->CreateEntity("FixedCamera_1");
        auto& cameraComp = m_Camera.AddComponent<CameraComponent>();
        cameraComp.FixedAspectRatio = true;

        m_SecondCamera = m_ActiveScene->CreateEntity("UnFixedCamera_1");
        auto& cc = m_SecondCamera.AddComponent<CameraComponent>();
        cc.Primary = false;

        m_Camera.AddComponent<NativeScriptComponent>().Bind<CameraControllerScript>();
        m_SecondCamera.AddComponent<NativeScriptComponent>().Bind<CameraControllerScript>();
        //===============创建面板===============================
        m_SceneHierachyPanel.SetContext(m_ActiveScene);


        //===============初始化序列化器=========================
        SceneSerializer serializer(m_ActiveScene);
        serializer.Deserialize("assets/scenes/Example.redflag");
#endif
        //===============创建EditorCamera===============================
        m_EditorCamera = EditorCamera(30.f, 1.788, 0.1f, 1000.f);
    }

    void ForgeLayer::OnDetach()
    {
        RF_PROFILE_FUNCTION();
    }

    void ForgeLayer::OnUpdate(Timestep ts)
    {
        RF_PROFILE_FUNCTION();

        // Camera & Stats 更新
        if (m_ViewportFocused) {
            m_CameraController.OnUpdate(ts);
            m_EditorCamera.OnUpdate(ts);
        }
        Renderer2D::ResetStats();

        // 渲染开始
        m_Framebuffer->Bind();
        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
        RenderCommand::Clear();
        m_Framebuffer->ClearAttachment(1, -1);

        m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);

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
            // 关键：如果你正在拖拽 Gizmo，不要更新 HoveredEntity，防止鼠标滑出物体导致失去焦点
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
                else {
                    m_HoveredEntity = {};
                }
            }
        }

        m_Framebuffer->Unbind(); // 确保在 if 外面，解决闪屏
    }

    void ForgeLayer::OnImGuiRender()
    {
        RF_PROFILE_FUNCTION();

        // ======================================================
        // DOCKING 模式 - 所有窗口都可以停靠
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
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        }

        ImGui::Begin("DockSpace Demo", nullptr, window_flags);
        if (opt_fullscreen) ImGui::PopStyleVar(2);

        // DockSpace
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f));
        }

        // ======================================================
        // 顶部菜单栏：处理文件 IO 逻辑
        // ======================================================
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                // 使用统一的成员函数处理逻辑
                if (ImGui::MenuItem("New", "Ctrl+N"))
                {
                    NewScene();
                }

                if (ImGui::MenuItem("Open...", "Ctrl+O"))
                {
                    OpenScene();
                }

                if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
                {
                    SaveSceneAs();
                }

                // 建议增加一个 Save (Ctrl+S)，这样更像真正的编辑器
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                {
                    SaveScene();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Exit"))
                {
                    Application::Get().Close();
                }

                ImGui::EndMenu();  // 结束 File 菜单
            }

            // 可以在这里添加其他菜单，比如 Edit、View 等
            // if (ImGui::BeginMenu("Edit")) { ... ImGui::EndMenu(); }

            ImGui::EndMenuBar();  // 结束菜单栏
        }

        // ======================================================
        // 实际 DOCKSPACE 内容
        // ======================================================

        /*Profile*/
        {
            RF_PROFILE_SCOPE("Stas");

            ImGui::Begin("Profile");

            std::string name = "None";
            if (m_HoveredEntity)
                name = m_HoveredEntity.GetComponent<TagComponent>().Tag;
            ImGui::Text("Hovered Entity:%s", name.c_str());

            auto stats = Renderer2D::GetStats();
            ImGui::Text("Draw Calls: %d", stats.DrawCalls);
            ImGui::Text("Quads: %d", stats.QuadCount);
            ImGui::Text("Vertices:%d", stats.GetTotalVertexCount());
            ImGui::Text("Indices:%d", stats.GetTotalIndexCount());

            ImGui::End();
        }
        /*End Profile*/

        /*Scene Hierachy*/
        m_SceneHierachyPanel.OnImguiRender();
        /*End Scene Hierachy*/

        /*Viewport*/
        /* Viewport 窗口 */
        ImGui::Begin("Viewport");

        // 1. 立即计算视口边界（必须在 Image 渲染前获取，确保坐标对齐）
        auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
        auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
        auto viewportOffset = ImGui::GetWindowPos();
        m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
        m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();
        Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused || !m_ViewportHovered);

        // 2. 自动 Resize 逻辑
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
        ImGui::Image((void*)(uint64_t)textureID, ImVec2(m_ViewportSize.x, m_ViewportSize.y), ImVec2(0, 1), ImVec2(1, 0));

        /* Gizmos 逻辑 */
        Entity selectedEntity = m_SceneHierachyPanel.GetSelectedEntity();
        if (selectedEntity && m_GizmoType != -1)
        {
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();

            // 使用之前精确计算的 m_ViewportBounds，解决“手感不对”的偏移问题
            ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
                m_ViewportBounds[1].x - m_ViewportBounds[0].x,
                m_ViewportBounds[1].y - m_ViewportBounds[0].y);

            const glm::mat4& cameraProjection = m_EditorCamera.GetProjection();
            glm::mat4 cameraView = m_EditorCamera.GetViewMatrix();

            auto& tc = selectedEntity.GetComponent<TransformComponent>();
            glm::mat4 transform = tc.GetTransform();

            bool snap = Input::IsKeyPressed(RF_KEY_LEFT_CONTROL);
            float snapValue = (m_GizmoType == ImGuizmo::OPERATION::ROTATE) ? 45.0f : 0.5f;
            float snapValues[3] = { snapValue, snapValue, snapValue };

            ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                (ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL, glm::value_ptr(transform),
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

        bool control = Input::IsKeyPressed(RF_KEY_LEFT_CONTROL) || Input::IsKeyPressed(RF_KEY_RIGHT_CONTROL);
        bool shift = Input::IsKeyPressed(RF_KEY_LEFT_SHIFT) || Input::IsKeyPressed(RF_KEY_RIGHT_SHIFT);

        switch (e.GetKeyCode())
        {
            case RF_KEY_N:
            {
                if (control)
                    NewScene();
                break;
            }
            case RF_KEY_O:
            {
                if (control)
                 OpenScene();
                break;
            }
            case RF_KEY_S:
            {
                if (control && shift)
                    SaveSceneAs();
                else if (control) SaveScene();
                break;
            }

            //Gizmos
            case RF_KEY_Q:
                m_GizmoType = -1;
                break;
            case RF_KEY_W:
                m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
                break;
            case RF_KEY_R:
                m_GizmoType = ImGuizmo::OPERATION::ROTATE;
                break;
            case RF_KEY_E:
                m_GizmoType = ImGuizmo::OPERATION::SCALE;
                break;
        }
    }

    bool ForgeLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (e.GetMouseButton() == RF_MOUSE_BUTTON_LEFT)
        {
            // 修改点：只有【鼠标在视口内】且【没在点 Gizmo 轴】且【没在旋转相机】时才切换选中
            if (m_ViewportHovered && !ImGuizmo::IsOver() && !Input::IsKeyPressed(RF_KEY_LEFT_ALT))
            {
                m_SceneHierachyPanel.SetSelectedEntity(m_HoveredEntity);
            }
        }
        return false;
    }


   

    void ForgeLayer::SaveScene()
    {
        // 如果已经有文件路径，直接保存
        if (!m_SceneFilePath.empty())
        {
            SceneSerializer serializer(m_ActiveScene);
            serializer.Serialize(m_SceneFilePath);
            RF_CORE_INFO("Scene saved: {0}", m_SceneFilePath);
        }
        else
        {
            // 第一次保存，需要选择保存位置
            SaveSceneAs();
        }
    }

    void ForgeLayer::SaveSceneAs()
    {
        std::string filepath = FileDialogs::SaveFile("Redflag Scene (*.redflag)\0*.redflag\0");

        if (!filepath.empty())
        {
            // 确保文件扩展名
            if (filepath.find(".redflag") == std::string::npos)
                filepath += ".redflag";

            SceneSerializer serializer(m_ActiveScene);
            serializer.Serialize(filepath);

            // 更新当前文件路径
            m_SceneFilePath = filepath;
            RF_CORE_INFO("Scene saved as: {0}", filepath);
        }
    }

    
    void Redflag::ForgeLayer::OpenScene()
    {
        std::string filepath = FileDialogs::OpenFile("Redflag Scene (*.redflag)\0*.redflag\0");

        if (!filepath.empty())
        {
            // --- 核心修复：清理状态 ---
            m_HoveredEntity = {}; // 清空悬停实体
            // -----------------------

            m_ActiveScene = CreateRef<Scene>();
            m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_SceneHierachyPanel.SetContext(m_ActiveScene);

            SceneSerializer serializer(m_ActiveScene);
            if (serializer.Deserialize(filepath))
            {
                m_SceneFilePath = filepath;
                RF_CORE_INFO("Successfully opened scene: {0}", filepath);
            }
        }
    }

    void Redflag::ForgeLayer::NewScene()
    {
        RF_CORE_INFO("Creating new scene...");

        // --- 核心修复：清理状态 ---
        m_HoveredEntity = {}; // 清空悬停实体
        // -----------------------

        m_ActiveScene = CreateRef<Scene>();
        m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        m_SceneHierachyPanel.SetContext(m_ActiveScene);

        m_SceneFilePath.clear();

        // 立即清空当前 Framebuffer，防止鼠标拾取到上一帧的残留 ID
        m_Framebuffer->Bind();
        m_Framebuffer->ClearAttachment(1, -1);
        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        RenderCommand::Clear();
        m_Framebuffer->Unbind();

        RF_CORE_INFO("New empty scene created");
    }
}
