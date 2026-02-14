#include "rfpch.h"
#include <GLFW/glfw3.h>
#include <glad/glad.h>
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
		RF_CORE_INFO("{0},{1},{2}", props.Title, props.Width, props.Height);

		//��ʼ��opengl����m_window������������Ϊ�ճ�ʼ���Ĵ���m_window�����Ұ�data����m_window����֮��ȡ��
		if (!s_GLFWInitialized) {
			int success = glfwInit();
			RF_CORE_ASSERT(success, "GLFWinit success");

			//����ص�����
			glfwSetErrorCallback(GLFWErrorCallback);
			s_GLFWInitialized = true;

		}

		//����glfw���ڲ�ָ��������
		m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);
		
		//�����ڴ���glfw�����ĺ󴴽�glad����
		// ʹ���µ���Ⱦ������
		m_Context = new OpenGLContext(m_Window);
		m_Context->Init();
		

		//����ǰ���ڴ�m_Dataָ�룬����֮��ص�ʱ��ȡ�������Ϣ�ͻص���Ա����
		glfwSetWindowUserPointer(m_Window, &m_Data);
		SetVSync(true);

		//----------------------------------------------------------����opengl�ص�-----------------------------------------------//

		// ���û���ק�������ڴ�Сʱ��GLFW����ô˺���
		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
			{
				// glfwGetWindowUserPointer�õ�data������void*����Ҫת��ΪWindowData*����
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				// ���´��������еĿ��Ⱥ͸߶���Ϣ
				data.Width = width;
				data.Height = height;


				// ��GLFW��ԭʼ������װ�������WindowResizeEvent�¼�
				WindowResizeEvent event(width, height);

				RF_CORE_WARN("{0},{1}", width, height);

				// �ᴥ��data�д洢��Application::OnEvent���������ݴ��ڴ�С�ı��¼�
				data.EventCallback(event);
			});

		// ���ڹرջص�
		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
			{
				// glfwGetWindowUserPointer�õ�data������void*����Ҫת��ΪWindowData*����
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				WindowCloseEvent event;

				// �ᴥ��data�д洢��Application::OnEvent���������ݴ��ڹر��¼�
				data.EventCallback(event);
			});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int modes)
			{
				// glfwGetWindowUserPointer�õ�data������void*����Ҫת��ΪWindowData*����
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				switch (action)
				{
				case GLFW_PRESS:
				{
					//��װ�¼�
					KeyPressedEvent event(key, 0);
					//application�ص����ַ��¼�
					data.EventCallback(event);
					break;
				}

				case GLFW_RELEASE:
				{
					//��װ�¼�
					KeyReleasedEvent event(key);
					//application�ص����ַ��¼�
					data.EventCallback(event);
					break;
				}

				case GLFW_REPEAT:
				{
					//��װ�¼�
					KeyPressedEvent event(key, 1);
					//application�ص����ַ��¼�
					data.EventCallback(event);
					break;
				}
				}
			});

		//�ַ��ص�
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
		//��ѯ�¼�
		glfwPollEvents();
		//����������
		m_Context->SwapBuffers();


	}
	//----------------------------------------------------------����opengl�ص�-----------------------------------------------//
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