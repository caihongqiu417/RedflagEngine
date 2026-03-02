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
    }

    void ForgeLayer::OnDetach()
    {
        RF_PROFILE_FUNCTION();
    }

    void ForgeLayer::OnUpdate(Timestep ts)
    {
        RF_PROFILE_FUNCTION();

        /*
        * Resize:视口（用户拖拽）和画布大小不一致时:
        * ①调节画布大小为视口大小
        * ②更新宽高比不固定的相机为视口比例
        */

        /*
        if (FramebufferSpecification spec = m_Framebuffer->GetSpecifictaion();
            m_ViewportSize.x>0.0f&&m_ViewportSize.y>0.0f&&
            (spec.Width != m_ViewportSize.x || spec.Height != m_ViewportSize.y))
        {
            m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        }
        */

        //Camera
        if (m_ViewportFocused && m_ViewportHovered)
        {
            m_CameraController.OnUpdate(ts);
        }

        // Stat
        Renderer2D::ResetStats();

        //Render Prep
        m_Framebuffer->Bind();
        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
        RenderCommand::Clear();

        //Render Update（ECS）
        //包括BeginScene,相机更新绘制，EndScene
        m_ActiveScene->OnUpdate(ts);

        m_Framebuffer->Unbind();
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
        ImGui::Begin("Viewport");

        // 只有 焦点 + 悬停 同时满足才不拦截->事件会传播给其他层
        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();
        Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused || !m_ViewportHovered);

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (m_ViewportSize != *((glm::vec2*)&viewportPanelSize))
        {
            // 1. 先更新成员变量（记录新尺寸）
            m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };
            // 2. 更新相机（逻辑层）
            m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            // 3. 最后更新 Framebuffer（渲染层）
            m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        }

        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
        ImGui::Image(
            (void*)(uint64_t)textureID,
            ImVec2(m_ViewportSize.x, m_ViewportSize.y),
            ImVec2(0, 1),  // 左上角对应OpenGL纹理的左下角
            ImVec2(1, 0)   // 右下角对应OpenGL纹理的右上角
        );


        /* Gizmo - 实体操纵工具 */
        Entity selectedEntity = m_SceneHierachyPanel.GetSelectedEntity();

        if (selectedEntity&&m_GizmoType!=-1)  // 有选中实体才显示Gizmo
        {
            // 设置Gizmo显示区域
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                (float)ImGui::GetWindowWidth(), (float)ImGui::GetWindowHeight());

            // 获取摄像机视图投影矩阵
            auto cameraEntity = m_ActiveScene->GetPrimaryCameraEntity();
            const auto& camera = cameraEntity.GetComponent<CameraComponent>().Camera;
            const glm::mat4& cameraProjection = camera.GetProjection();
            glm::mat4 cameraView = glm::inverse(cameraEntity.GetComponent<TransformComponent>().GetTransform());

            // 获取实体当前变换矩阵
            auto& tc = selectedEntity.GetComponent<TransformComponent>();
            glm::mat4 transform = tc.GetTransform();
            
            // 吸附
            bool snap = Input::IsKeyPressed(RF_KEY_LEFT_CONTROL);
            // 默认吸附步长值
            float snapValue = 0.5f;
            // 如果是旋转模式，吸附步长改为45度
            if (m_GizmoType == ImGuizmo::OPERATION::ROTATE)
                snapValue = 45.f;

            float snapValues[3] = { snapValue,snapValue,snapValue };

            // 显示Gizmo并处理操作
            ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                (ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL, glm::value_ptr(transform),nullptr,snap?snapValues:nullptr);

            if (ImGuizmo::IsUsing())  // 如果正在使用Gizmo
            {
                // 从Gizmo修改后的矩阵分解出位置、旋转和缩放
                glm::vec3 translation, rotation, scale;
                Math::DecomposeTransform(transform, translation, rotation, scale);

                // 计算旋转增量并处理角度环绕（如 350°到10°应该+20°而不是-340°）
                glm::vec3 deltaRotation = rotation - tc.Rotation;

                // 更新变换组件
                tc.Translation = translation;           // 直接赋值位置
                tc.Rotation += deltaRotation;            // 增量更新旋转（保持平滑性）
                tc.Scale = scale;                        // 直接赋值缩放
            }
        }





        ImGui::End();  // 结束 Viewport
        /*End Viewport*/

        ImGui::End();  // 结束 DockSpace
    }

    void ForgeLayer::OnEvent(Event& event)
    {
        m_CameraController.OnEvent(event);

        EventDispatcher dispatcher(event);
        dispatcher.Dispatch<KeyPressedEvent>(RF_BIND_EVENT_FN(ForgeLayer::OnKeyPressed));
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

    void ForgeLayer::NewScene()
    {
        // 1. 创建一个新的场景实例
        m_ActiveScene = CreateRef<Scene>();

        // 2. 关键：必须同步视口大小
        // 如果不调用这个，新场景里的相机组件在渲染时会因为 AspectRatio 为 0 导致画面消失
        m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

        // 3. 让层级面板（Hierarchy Panel）重新指向这个新场景
        m_SceneHierachyPanel.SetContext(m_ActiveScene);
    }

    void Redflag::ForgeLayer::OpenScene()
    {
        // 1. 弹出文件选择对话框
        // 过滤后缀为 .redflag 的文件
        std::string filepath = FileDialogs::OpenFile("Redflag Scene (*.redflag)\0*.redflag\0");

        if (!filepath.empty())
        {
            // 2. 重置当前活动场景
            m_ActiveScene = CreateRef<Scene>();

            // 3. 必须同步当前视口大小，确保新场景相机比例正确
            m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

            // 4. 更新层级面板的上下文（指向新创建的场景）
            m_SceneHierachyPanel.SetContext(m_ActiveScene);

            // 5. 执行反序列化（从磁盘加载数据到 m_ActiveScene）
            SceneSerializer serializer(m_ActiveScene);
            if (serializer.Deserialize(filepath))
            {
                // 6. 成功加载后记录文件路径，以便后续 Ctrl+S 直接保存
                m_SceneFilePath = filepath;
                RF_CORE_INFO("Successfully opened scene: {0}", filepath);
            }
        }
    }

    void Redflag::ForgeLayer::SaveSceneAs()
    {
        // 1. 弹出文件保存对话框
        std::string filepath = FileDialogs::SaveFile("Redflag Scene (*.redflag)\0*.redflag\0");

        if (!filepath.empty())
        {
            // 2. 执行序列化（将当前内存中的 m_ActiveScene 写入磁盘）
            SceneSerializer serializer(m_ActiveScene);
            serializer.Serialize(filepath);

            // 3. 更新当前文件路径，后续 Ctrl+S 将不再弹出窗口
            m_SceneFilePath = filepath;
            RF_CORE_INFO("Scene saved as: {0}", filepath);
        }
    }

    void ForgeLayer::SaveScene()
    {
        // 如果 m_SceneFilePath 不为空，说明是从磁盘打开的或者已经 SaveAs 过一次了
        if (!m_SceneFilePath.empty())
        {
            SceneSerializer serializer(m_ActiveScene);
            serializer.Serialize(m_SceneFilePath);
            RF_CORE_INFO("Scene saved: {0}", m_SceneFilePath);
        }
        else
        {
            // 第一次保存，必须让用户选地方
            SaveSceneAs();
        }
    }
}
