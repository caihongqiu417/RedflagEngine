#include"rfpch.h"
#include"ImGuiLayer.h"

#include"imgui.h"
#include"backends/imgui_impl_glfw.h"
#include"backends/imgui_impl_opengl3.h"

#include"Redflag/Core/Application.h"

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
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;    // 启用多视口支持

        //字体
        io.FontDefault = io.Fonts->AddFontFromFileTTF("assets/fonts/Bitter/static/Bitter-SemiBold.ttf", 18.0f);

        const std::filesystem::path chineseFontPath = FindChineseFontPath();
        if (!chineseFontPath.empty())
        {
            ImFontConfig chineseFontConfig;
            chineseFontConfig.MergeMode = true;
            chineseFontConfig.PixelSnapH = true;
            io.Fonts->AddFontFromFileTTF(chineseFontPath.string().c_str(), 18.0f, &chineseFontConfig,
                io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        }
        else
        {
            RF_CORE_WARN("No Chinese font found. ImGui Chinese text may render as '?'.");
        }

        // 设置ImGui样式为深色主题
        SetThemeColor();

        // 调整样式以适应多视口
        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;  // 窗口圆角为0
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;  // 窗口背景不透明度为1
        }

        // 获取应用和窗口实例
        Application& app = Application::Get();
        GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());

        // 初始化ImGui后端
        ImGui_ImplGlfw_InitForOpenGL(window, true);  // GLFW后端
        ImGui_ImplOpenGL3_Init("#version 410");      // OpenGL 4.1后端
    }

	void ImGuiLayer::OnDetach()
	{

        RF_PROFILE_FUNCTION();
        // 当ImGui层从层栈分离时调用，清理ImGui资源
        ImGui_ImplOpenGL3_Shutdown();  // 关闭OpenGL后端
        ImGui_ImplGlfw_Shutdown();     // 关闭GLFW后端
        ImGui::DestroyContext();       // 销毁ImGui上下文
	}

    void ImGuiLayer::OnImGuiRender()
    {
       
    }

    void ImGuiLayer::OnEvent(Event& e)
    {
       
        if (m_BlockEvents) // 当鼠标在其他面板上时，m_BlockEvents = true
        {
            ImGuiIO& io = ImGui::GetIO();
            if ((e.IsInCategory(EventCategoryMouse) && io.WantCaptureMouse) ||
                (e.IsInCategory(EventCategoryKeyboard) && io.WantCaptureKeyboard))
            {
                e.SetHandled(true);
            }
        }
    }

    void ImGuiLayer::Begin() {

        RF_PROFILE_FUNCTION();
        // 开始一个新的ImGui帧
        ImGui_ImplOpenGL3_NewFrame();  // OpenGL新帧
        ImGui_ImplGlfw_NewFrame();     // GLFW新帧
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
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());  // 使用OpenGL渲染

        // 如果启用了多视口，更新平台窗口
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();  // 备份当前上下文
            ImGui::UpdatePlatformWindows();     // 更新平台窗口
            ImGui::RenderPlatformWindowsDefault();  // 渲染平台窗口
            glfwMakeContextCurrent(backup_current_context);  // 恢复上下文
        }
    }

    //=======================================================================
    //配色方案
    //=======================================================================
    void ImGuiLayer::SetThemeColor()
    {
        auto& colors = ImGui::GetStyle().Colors;
        auto& style = ImGui::GetStyle();

        // 背景色系
        colors[ImGuiCol_WindowBg] = ImVec4(0.89f, 0.90f, 0.91f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.92f, 0.93f, 0.94f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.86f, 0.87f, 0.88f, 0.98f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.88f, 0.89f, 0.90f, 1.00f);

        // 边框
        colors[ImGuiCol_Border] = ImVec4(0.74f, 0.75f, 0.76f, 0.50f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        // 标题栏
        colors[ImGuiCol_TitleBg] = ImVec4(0.87f, 0.88f, 0.89f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.86f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.89f, 0.90f, 0.91f, 0.90f);

        // 头部
        colors[ImGuiCol_Header] = ImVec4(0.86f, 0.18f, 0.18f, 0.70f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.90f, 0.22f, 0.22f, 0.80f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.95f, 0.15f, 0.15f, 0.90f);

        // 按钮
        colors[ImGuiCol_Button] = ImVec4(0.86f, 0.18f, 0.18f, 0.85f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.92f, 0.22f, 0.22f, 0.95f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.78f, 0.14f, 0.14f, 1.00f);

        // 输入框背景
        colors[ImGuiCol_FrameBg] = ImVec4(0.88f, 0.89f, 0.90f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.84f, 0.85f, 0.86f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.80f, 0.81f, 0.82f, 1.00f);

        // Tab
        colors[ImGuiCol_Tab] = ImVec4(0.89f, 0.90f, 0.91f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.86f, 0.18f, 0.18f, 0.50f);
        colors[ImGuiCol_TabActive] = ImVec4(0.86f, 0.18f, 0.18f, 0.85f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.91f, 0.92f, 0.93f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.86f, 0.18f, 0.18f, 0.40f);

        // 文字
        colors[ImGuiCol_Text] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.61f, 0.63f, 1.00f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.86f, 0.18f, 0.18f, 0.20f);

        // 滚动条
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.91f, 0.92f, 0.93f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.73f, 0.74f, 0.75f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.86f, 0.18f, 0.18f, 0.50f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.86f, 0.18f, 0.18f, 0.85f);

        // 勾选框
        colors[ImGuiCol_CheckMark] = ImVec4(0.86f, 0.18f, 0.18f, 1.00f);

        // 滑块
        colors[ImGuiCol_SliderGrab] = ImVec4(0.86f, 0.18f, 0.18f, 0.75f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.92f, 0.22f, 0.22f, 0.90f);

        // 分隔符
        colors[ImGuiCol_Separator] = ImVec4(0.74f, 0.75f, 0.76f, 0.50f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.86f, 0.18f, 0.18f, 0.60f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.86f, 0.18f, 0.18f, 0.90f);

        // 调整大小手柄
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.86f, 0.18f, 0.18f, 0.15f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.86f, 0.18f, 0.18f, 0.50f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.86f, 0.18f, 0.18f, 0.85f);

        // 图表
        colors[ImGuiCol_PlotLines] = ImVec4(0.86f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.92f, 0.22f, 0.22f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.86f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.92f, 0.22f, 0.22f, 1.00f);

        colors[ImGuiCol_DragDropTarget] = ImVec4(0.86f, 0.18f, 0.18f, 0.85f);
        colors[ImGuiCol_NavHighlight] = ImVec4(0.86f, 0.18f, 0.18f, 1.00f);

        // 样式参数
        style.WindowRounding = 8.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 6.0f;
        style.PopupRounding = 6.0f;
        style.ScrollbarRounding = 10.0f;
        style.GrabRounding = 6.0f;
        style.TabRounding = 6.0f;
        style.WindowTitleAlign = ImVec2(0.5f, 0.5f);

        style.WindowPadding = ImVec2(15.0f, 15.0f);
        style.FramePadding = ImVec2(12.0f, 6.0f);
        style.ItemSpacing = ImVec2(12.0f, 8.0f);
        style.ItemInnerSpacing = ImVec2(10.0f, 6.0f);
        style.IndentSpacing = 22.0f;

        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;

        style.WindowMenuButtonPosition = ImGuiDir_Right;
        style.ColorButtonPosition = ImGuiDir_Left;
    }
}