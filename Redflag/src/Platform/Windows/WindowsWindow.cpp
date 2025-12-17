#include "rfpch.h"
#include <GLFW/glfw3.h> 
#include "WindowsWindow.h"

#include"Redflag/Events/KeyEvent.h"
#include"Redflag/Events/MouseEvent.h"
#include"Redflag/Events/ApplicationEvent.h"

#include"Platform/OpenGL/OpenGLContext.h"

namespace Redflag {

	static bool s_GLFWInitialized = false;

	static void GLFWErrorCallback(int error, const char* description)
	{
		RF_CORE_ERROR("GLFW Error ({0}):{1}", error, description);
	}

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

		//初始化opengl窗口m_window，设置上下文为刚初始化的窗口m_window，并且把data存入m_window便于之后取出
		if (!s_GLFWInitialized) {
			int success = glfwInit();
			RF_CORE_ASSERT(success, "GLFW初始化失败");

			//错误回调函数
			glfwSetErrorCallback(GLFWErrorCallback);
			s_GLFWInitialized = true;

		}

		//创建glfw窗口并指定上下文
		m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);
		
		//必须在创建glfw上下文后创建glad！！
		// 使用新的渲染上下文
		m_Context = new OpenGLContext(m_Window);
		m_Context->Init();
		

		//给当前窗口存m_Data指针，用于之后回调时获取保存的信息和回调成员函数
		glfwSetWindowUserPointer(m_Window, &m_Data);
		SetVSync(true);

		//----------------------------------------------------------设置opengl回调-----------------------------------------------//

		// 当用户拖拽调整窗口大小时，GLFW会调用此函数
		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
			{
				// glfwGetWindowUserPointer得到data，返回void*，需要转换为WindowData*类型
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				// 更新窗口数据中的宽度和高度信息
				data.Width = width;
				data.Height = height;

				// 将GLFW的原始参数封装成引擎的WindowResizeEvent事件
				WindowResizeEvent event(width, height);

				// 会触发data中存储的Application::OnEvent函数，传递窗口大小改变事件
				data.EventCallback(event);
			});

		// 窗口关闭回调
		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
			{
				// glfwGetWindowUserPointer得到data，返回void*，需要转换为WindowData*类型
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				WindowCloseEvent event;

				// 会触发data中存储的Application::OnEvent函数，传递窗口关闭事件
				data.EventCallback(event);
			});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int modes)
			{
				// glfwGetWindowUserPointer得到data，返回void*，需要转换为WindowData*类型
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				switch (action)
				{
				case GLFW_PRESS:
				{
					//封装事件
					KeyPressedEvent event(key, 0);
					//application回调，分发事件
					data.EventCallback(event);
					break;
				}

				case GLFW_RELEASE:
				{
					//封装事件
					KeyReleasedEvent event(key);
					//application回调，分发事件
					data.EventCallback(event);
					break;
				}

				case GLFW_REPEAT:
				{
					//封装事件
					KeyPressedEvent event(key, 1);
					//application回调，分发事件
					data.EventCallback(event);
					break;
				}
				}
			});

		//字符回调
		glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int codepoint) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			KeyTypedEvent event(codepoint);
			data.EventCallback(event);
			});


		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			switch (action)
			{
			case GLFW_PRESS:
			{
				MouseButtonPressedEvent event(button);
				data.EventCallback(event);
				break;
			}

			case GLFW_RELEASE:
			{
				MouseButtonReleasedEvent event(button);
				data.EventCallback(event);
				break;
			}
			}
			});
		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xoffset, double yoffset) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			MouseScrolledEvent event((float)xoffset, (float)yoffset);
			data.EventCallback(event);
			});
		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xpos, double ypos) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			MouseMovedEvent event((float)xpos, (float)ypos);
			data.EventCallback(event);
			});

	}

	void WindowsWindow::ShutDown() {
		glfwDestroyWindow(m_Window);
	}
	void WindowsWindow::OnUpdate()
	{
		//轮询事件
		glfwPollEvents();
		//交换缓冲区
		m_Context->SwapBuffers();


	}
	//----------------------------------------------------------设置opengl回调-----------------------------------------------//
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