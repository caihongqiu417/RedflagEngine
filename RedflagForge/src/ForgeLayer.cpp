#include "rfpch.h"

#include"Redflag.h"

#include "Platform/OpenGL/OpenGLShader.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Redflag/Renderer/Shader.h"
#include"Redflag/Scene/CameraControllerScript.h"
#include "Redflag/Scene/SceneSerializer.h"
#include "Redflag/Utils/PlatformUtils.h" 


#include "ForgeLayer.h"


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
        auto& cameraComp=m_Camera.AddComponent<CameraComponent>();
        cameraComp.FixedAspectRatio = true;

        m_SecondCamera = m_ActiveScene->CreateEntity("UnFixedCamera_1");
        auto& cc=m_SecondCamera.AddComponent<CameraComponent>();
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

           // 菜单栏
         // ======================================================
        // 顶部菜单栏：处理文件 IO 逻辑
        // ======================================================
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                // 【新建场景】：清空当前场景并重置面板
                if (ImGui::MenuItem("New", "Ctrl+N"))
                {
                    m_ActiveScene = CreateRef<Scene>();
                    // 必须同步视口大小，否则新场景的相机投影矩阵是错的
                    m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
                    m_SceneHierachyPanel.SetContext(m_ActiveScene);
                }

                // 【打开场景】：从磁盘加载 .redflag 文件
                if (ImGui::MenuItem("Load...", "Ctrl+O"))
                {
                    std::string filepath = FileDialogs::OpenFile("Redflag Scene (*.redflag)\0*.redflag\0\0");
                    if (!filepath.empty())
                    {
                        // 1. 创建新场景实例
                        m_ActiveScene = CreateRef<Scene>();
                        m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

                        // 2. 将 UI 面板关联到新场景
                        m_SceneHierachyPanel.SetContext(m_ActiveScene);

                        // 3. 反序列化（将文件内容填入场景）
                        SceneSerializer serializer(m_ActiveScene);
                        serializer.Deserialize(filepath);
                    }
                }

                // 【另存为】：将当前内存中的场景序列化到磁盘
                if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
                {
                    std::string filepath = FileDialogs::SaveFile("Redflag Scene (*.redflag)\0*.redflag\0\0");
                    if (!filepath.empty())
                    {
                        SceneSerializer serializer(m_ActiveScene);
                        serializer.Serialize(filepath);
                    }
                }

                ImGui::Separator(); // 分隔线

                if (ImGui::MenuItem("Exit"))
                {
                    Application::Get().Close();
                }

                ImGui::EndMenu();
            }
       
            ImGui::EndMenuBar();
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

        //只有 焦点 + 悬停 同时满足才不拦截->事件会传播给其他层
        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();
        Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused || !m_ViewportHovered);

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (m_ViewportSize != *((glm::vec2*)&viewportPanelSize))
        {
            // 1. 先更新成员变量（记录新尺寸）
            m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };
            // 2. 更新相机（逻辑层）
                /*
                * Resize:视口（用户拖拽）和画布大小不一致时:
                * ①调节画布大小为视口大小
                * ②更新宽高比不固定的相机为视口比例
                 */
            //m_CameraController.OnResize(viewportPanelSize.x, viewportPanelSize.y);
            m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            // 3. 最后更新 Framebuffer（渲染层）
            m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y); 
            
        }

        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
        ImGui::Image(
            (void*)(uint64_t)textureID,
            ImVec2(m_ViewportSize.x,m_ViewportSize.y),
            ImVec2(0, 1),  // 左上角对应OpenGL纹理的左下角
            ImVec2(1, 0)   // 右下角对应OpenGL纹理的右上角
        );

        ImGui::End();
        /*End Viewport*/

        ImGui::End();
    }

    void ForgeLayer::OnEvent(Event& event)
    {
        m_CameraController.OnEvent(event);
    }
}