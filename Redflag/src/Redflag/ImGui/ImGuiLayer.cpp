#include"rfpch.h"
#include"ImGuiLayer.h"

#include"imgui.h"
#include"backends/imgui_impl_glfw.h"
#include"backends/imgui_impl_opengl3.h"

#include"Redflag/Core/Application.h"

//TEMPORARY
#include<GLFW/glfw3.h>
#include<glad/glad.h>

namespace Redflag {
	ImGuiLayer::ImGuiLayer()
		:Layer("ImGuiLayer")
	{

	}
	ImGuiLayer::~ImGuiLayer()
	{

	}

    void ImGuiLayer::OnAttach()
    {
        RF_PROFILE_FUNCTION();

        // 当ImGui层被附加到层栈时调用，初始化ImGui

       // 检查ImGui版本
        IMGUI_CHECKVERSION();
        // 创建ImGui上下文
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;

        // 设置ImGui配置标志
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // 启用键盘导航
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // 启用窗口停靠功能
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;    // 启用多视口支持

        // 设置ImGui样式为深色主题
        ImGui::StyleColorsDark();

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
}