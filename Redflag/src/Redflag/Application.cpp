#include "hzpch.h"
#include "Application.h"
#include "Redflag/Log.h"

//test for event
#include "Events/ApplicationEvent.h"
#include<GLFW/glfw3.h>


namespace Redflag
{
	Application::Application() {
		m_Window = std::unique_ptr<Window>(Window::Create());
	}
	Application::~Application() {

	}
	void Application::Run() {
		WindowResizeEvent e(1280, 720);
		if (e.IsInCategory(EventCategoryApplication)) {
			RF_CORE_TRACE("Window resize event triggered"); // 使用英文避免编码问题
		}
		if (e.IsInCategory(EventCategoryInput)) {
			RF_CORE_ERROR("Event category check: Window resize should not be in input category");  // 使用英文
		}

		while (m_Running)
		{
			glClearColor(1, 0, 1, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			m_Window->OnUpdate();
		}
	}

} // 添加这个结束命名空间的大括号