#include "hzpch.h"
#include "WindowsWindow.h"

namespace Redflag {

	static bool s_GLFWInitialized = false;

	Window* Window::Create(const WindowProps& props) {
		return new WindowsWindow(props);
	}
	WindowsWindow::WindowsWindow(const WindowProps& props) {
		Init(props);
	}
	WindowsWindow::~WindowsWindow() {
		ShutDown();
	}

	void WindowsWindow::Init(const WindowProps& props) {
		m_Data.Title = props.Title;
		m_Data.Width = props.Width;
		m_Data.Height = props.Height;
		RF_CORE_INFO("创建了{0},{1},{2}", props.Title, props.Width, props.Height);
		
		if (!s_GLFWInitialized) {
			int success = glfwInit();
			RF_CORE_ASSERT(success,"GLFW初始化失败");
			s_GLFWInitialized = true;

		}
		m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);
		glfwMakeContextCurrent(m_Window);
		glfwSetWindowUserPointer(m_Window, &m_Data);
		SetVSync(true);
	}
	void WindowsWindow::ShutDown() {
		glfwDestroyWindow(m_Window);
	}
	void WindowsWindow::OnUpdate()
	{
		//轮询事件
		glfwPollEvents();
		//交换缓冲区
		glfwSwapBuffers(m_Window);


	}
	void WindowsWindow::SetVSync(bool enable) {
		if (enable)
			glfwSwapInterval(1);
		else
			glfwSwapInterval(0);
		m_Data.VSync = enable;
	}
	bool WindowsWindow::IsVSync() const {
		return m_Data.VSync;
	}
}