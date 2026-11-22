#include"rfpch.h"
#include"ImGuiLayer.h"

#include"imgui.h"
#include "Redflag/Debug/PlaytestSession.h"
#include"backends/imgui_impl_glfw.h"
#include"backends/imgui_impl_opengl3.h"

#include"Redflag/Core/Application.h"
#include "Redflag/Core/Input.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "VulkanImGui.h"

//TEMPORARY
#include<GLFW/glfw3.h>
#include<glad/glad.h>
#include <filesystem>
#include"ImGuizmo.h"

namespace Redflag {
    namespace
    {
        static std::filesystem::path FindChineseFontPath()
        {
            const std::array<std::filesystem::path, 4> candidatePaths = {
                "C:/Windows/Fonts/msyh.ttc",
                "C:/Windows/Fonts/msyh.ttf",
                "C:/Windows/Fonts/simhei.ttf",
                "C:/Windows/Fonts/simsun.ttc"
            };

            for (const auto& candidatePath : candidatePaths)
            {
                if (std::filesystem::exists(candidatePath))
                    return candidatePath;
            }

            return {};
        }
    }

	ImGuiLayer::ImGuiLayer()
		:Layer("ImGuiLayer")
	{

	}
	ImGuiLayer::~ImGuiLayer()
	{

	}

    // 当ImGui层被附加到层栈时调用，初始化ImGui
    void ImGuiLayer::OnAttach()
    {
        RF_PROFILE_FUNCTION();

       // 检查ImGui版本
        IMGUI_CHECKVERSION();
        // 创建ImGui上下文
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;

        // 设置ImGui配置标志
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // 启用键盘导航
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // 启用窗口停靠功能
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL)
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;    // 启用多视口支持

        //字体
        Application& app = Application::Get();
        GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());
        float scaleX = 1.0f, scaleY = 1.0f;
        glfwGetWindowContentScale(window, &scaleX, &scaleY);
        const float fontSize = 18.0f * scaleY;
        io.FontDefault = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", fontSize);

        const std::filesystem::path chineseFontPath = FindChineseFontPath();
        if (!chineseFontPath.empty())
        {
            ImFontConfig chineseFontConfig;
            chineseFontConfig.MergeMode = true;
            chineseFontConfig.PixelSnapH = true;
            chineseFontConfig.OversampleH = 1;
            io.Fonts->AddFontFromFileTTF(chineseFontPath.string().c_str(), fontSize, &chineseFontConfig,
                io.Fonts->GetGlyphRangesChineseFull());
        }
        else
        {
            RF_CORE_WARN("No Chinese font found. ImGui Chinese text may render as '?'.");
        }

        // 设置ImGui样式为深色主题
        SetThemeColor();
        ImGui::GetStyle().ScaleAllSizes(scaleY);

        // 调整样式以适应多视口
        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;  // 窗口圆角为0
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;  // 窗口背景不透明度为1
        }

        // 获取应用和窗口实例

        // 初始化ImGui后端
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
        {
            io.IniFilename = "imgui-vulkan.ini";
            ImGui_ImplGlfw_InitForVulkan(window, true);
            auto& context = static_cast<VulkanContext&>(app.GetWindow().GetGraphicsContext());
            m_Vulkan = CreateScope<VulkanImGui>(context);
        }
        else
        {
            ImGui_ImplGlfw_InitForOpenGL(window, true);
            ImGui_ImplOpenGL3_Init("#version 410");
        }
    }

	void ImGuiLayer::OnDetach()
	{

        RF_PROFILE_FUNCTION();
        // 当ImGui层从层栈分离时调用，清理ImGui资源
        if (m_Vulkan)
            m_Vulkan.reset();
        else
            ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();     // 关闭GLFW后端
        ImGui::DestroyContext();       // 销毁ImGui上下文
	}

    void ImGuiLayer::OnImGuiRender()
    {
       
    }

    // ---- 游戏画面与工具窗口的输入 ----
    void ImGuiLayer::OnEvent(Event& e)
    {
       
        if (m_BlockEvents)
        {
            ImGuiIO& io = ImGui::GetIO();
            if ((e.IsInCategory(EventCategoryMouse) && io.WantCaptureMouse) ||
                (e.IsInCategory(EventCategoryKeyboard) && io.WantCaptureKeyboard))
            {
                e.SetHandled(true);
            }
        }
    }

    void ImGuiLayer::BlockEvents(bool block)
    {
        m_BlockEvents = block;
        if (!m_GameMouseButtons) m_GameMouseButtons.emplace();

        // 在游戏图内按下，移出后仍按住。从工具窗口开始的拖动留给该窗口。
        for (int button = 0; button < int(m_GameMouseButtons->size()); ++button)
        {
            if (ImGui::IsMouseClicked(button)) (*m_GameMouseButtons)[button] = !block;
            if (!ImGui::IsMouseDown(button)) (*m_GameMouseButtons)[button] = false;
        }
    }

    void ImGuiLayer::ReleaseGameMouse()
    {
        if (m_GameMouseButtons) m_GameMouseButtons->fill(false);
    }

    bool ImGuiLayer::IsGameMouseDown(int button) const
    {
        if (!m_GameMouseButtons) return Input::IsMouseButtonPressed(button);
        return button >= 0 && button < int(m_GameMouseButtons->size()) && (*m_GameMouseButtons)[button];
    }

    void ImGuiLayer::Begin() {

        RF_PROFILE_FUNCTION();
        // 开始一个新的ImGui帧
        if (m_Vulkan)
            m_Vulkan->NewFrame();
        else
            ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();     // GLFW新帧
        // GLFW 先提供真实设备输入。试玩工具的输入随后交给相同的按钮和窗口。
        if (PlaytestSession* session = PlaytestSession::Active()) session->SendToGui();
        ImGui::NewFrame();             // ImGui新帧

        //ImGuizmo
        ImGuizmo::BeginFrame();

    }

    void ImGuiLayer::End() 
    {
        RF_PROFILE_FUNCTION();

        // 结束当前ImGui帧并渲染
        ImGuiIO& io = ImGui::GetIO();
        Application& app = Application::Get();

        // 设置显示尺寸
        io.DisplaySize = ImVec2(
            static_cast<float>(app.GetWindow().GetWidth()),
            static_cast<float>(app.GetWindow().GetHeight())
        );

        // 渲染ImGui
        ImGui::Render();  // 生成绘制数据
        if (m_Vulkan)
            m_Vulkan->Render();
        else
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // 如果启用了多视口，更新平台窗口
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();  // 备份当前上下文
            ImGui::UpdatePlatformWindows();     // 更新平台窗口
            ImGui::RenderPlatformWindowsDefault();  // 渲染平台窗口
            glfwMakeContextCurrent(backup_current_context);  // 恢复上下文
        }
    }

    void ImGuiLayer::SetThemeColor()
    {
        auto& style = ImGui::GetStyle();
        ImGui::StyleColorsDark(&style);
        style.WindowPadding = { 9, 7 };
        style.FramePadding = { 7, 4 };
        style.ItemSpacing = { 7, 5 };
        style.TabRounding = 2;
        style.ScrollbarSize = 15;

        auto& colors = style.Colors;
        const ImVec4 panel{ .145f, .15f, .16f, 1 }, toolbar{ .105f, .11f, .12f, 1 };
        const ImVec4 field{ .225f, .23f, .245f, 1 }, hover{ .34f, .27f, .285f, 1 };
        const ImVec4 selected{ .48f, .17f, .205f, 1 }, accent{ .91f, .53f, .54f, 1 };
        colors[ImGuiCol_WindowBg] = panel;
        colors[ImGuiCol_ChildBg] = { 0, 0, 0, 0 };
        colors[ImGuiCol_PopupBg] = { .175f, .18f, .195f, 1 };
        colors[ImGuiCol_Text] = { .93f, .93f, .94f, 1 };
        colors[ImGuiCol_TextDisabled] = { .72f, .73f, .76f, 1 };
        for (auto slot : { ImGuiCol_MenuBarBg, ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_Tab, ImGuiCol_TabUnfocused })
            colors[slot] = toolbar;
        for (auto slot : { ImGuiCol_FrameBg, ImGuiCol_Button, ImGuiCol_TabUnfocusedActive })
            colors[slot] = field;
        for (auto slot : { ImGuiCol_FrameBgHovered, ImGuiCol_ButtonHovered, ImGuiCol_HeaderHovered, ImGuiCol_TabHovered })
            colors[slot] = hover;
        for (auto slot : { ImGuiCol_Header, ImGuiCol_FrameBgActive, ImGuiCol_ButtonActive, ImGuiCol_HeaderActive, ImGuiCol_TabActive })
            colors[slot] = selected;
        for (auto slot : { ImGuiCol_CheckMark, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_SeparatorActive, ImGuiCol_NavHighlight })
            colors[slot] = accent;
        colors[ImGuiCol_DockingPreview] = { .65f, .22f, .25f, .60f };
        colors[ImGuiCol_TextSelectedBg] = { .65f, .22f, .25f, .75f };
    }
}
