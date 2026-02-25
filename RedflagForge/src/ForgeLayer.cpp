#include "rfpch.h"

#include"Redflag.h"

#include "Platform/OpenGL/OpenGLShader.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Redflag/Renderer/Shader.h"

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

        auto square=m_ActiveScene->CreateEntity("Square");
        square.AddComponent<SpriteRendererComponent>(glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
        m_SquareEntity = square;

        m_CameraEntity = m_ActiveScene->CreateEntity("Camera Entity");
        m_CameraEntity.AddComponent<CameraComponent>();

        m_SecondCamera = m_ActiveScene->CreateEntity("ClipSpace Camera");
        auto& cc=m_SecondCamera.AddComponent<CameraComponent>();
        cc.Primary = false;


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
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit")) Application::Get().Close();
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // ======================================================
        // 实际 DOCKSPACE 内容
        // ======================================================

        /*Settings*/
        ImGui::Begin("Settings");

        auto stats = Renderer2D::GetStats();
        ImGui::Text("Draw Calls: %d", stats.DrawCalls);
        ImGui::Text("Quads: %d", stats.QuadCount);
        ImGui::Text("Vertices:%d", stats.GetTotalVertexCount());
        ImGui::Text("Indices:%d", stats.GetTotalIndexCount());

        if (m_SquareEntity)
        {
            ImGui::Separator();
            ImGui::Text("%s", m_SquareEntity.GetComponent<TagComponent>().Tag.c_str());
            auto& squareColor = m_SquareEntity.GetComponent<SpriteRendererComponent>().Color;
            ImGui::ColorEdit4("SquareColor", glm::value_ptr(squareColor));
        }

        ImGui::DragFloat3("Camera Transform", glm::value_ptr(m_CameraEntity.GetComponent<TransformComponent>().Transform[3]));
        if (ImGui::Checkbox("ChangeCamera", &m_PrimaryCamera))
        {
            m_CameraEntity.GetComponent<CameraComponent>().Primary = m_PrimaryCamera;
            m_SecondCamera.GetComponent<CameraComponent>().Primary = !m_PrimaryCamera;
        }

        {
            auto& camera = m_SecondCamera.GetComponent<CameraComponent>().Camera;
            float orthoSize = camera.GetOrthographicSize();
            if (ImGui::DragFloat("Second Camera Ortho Size", &orthoSize))
                camera.SetOrthographicSize(orthoSize);
        }
        

        ImGui::End();
        /*End Settings*/

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

