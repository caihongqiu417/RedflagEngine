#include "rfpch.h"
#include "Application.h"
#include "Redflag/Log.h"


#include "Events/ApplicationEvent.h"
#include"glad/glad.h"

#include"Input.h"




namespace Redflag
{
	#define BIND_EVENT_FN(x) std::bind(&Application::x,this,std::placeholders::_1)

	Application* Application::s_Instance = nullptr;

	Application::Application() 
	{
		RF_CORE_ASSERT(!s_Instance, "Application already exists!");
		s_Instance = this;

		m_Window = std::unique_ptr<Window>(Window::Create());

		//把成员函数onEvent存储到m_window的m_data中
		m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));

		m_ImGuiLayer = new ImGuiLayer();
		PushOverlay(m_ImGuiLayer);
	}
	Application::~Application() 
	{

	}

	void Application::PushLayer(Layer* layer)
	{
		m_LayerStack.PushLayer(layer);
		layer->OnAttach();
	}

	void Application::PushOverlay(Layer* layer)
	{
		m_LayerStack.PushOverlay(layer);
		layer->OnAttach();
	}

	void Application::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(OnWindowClose));
		RF_CORE_INFO("{0}", e.ToString());

		//从overlay从后往前传播事件
		for (auto it = m_LayerStack.end();it != m_LayerStack.begin();)
		{
			(*--it)->OnEvent(e);
			if (e.Handled())
				break;
		}
	}


	void Application::Run()
	{
		WindowResizeEvent e(1280, 720);
		if (e.IsInCategory(EventCategoryApplication)) {
			RF_CORE_TRACE("Window resize event triggered"); 
		}
		if (e.IsInCategory(EventCategoryInput)) {
			RF_CORE_ERROR("Event category check: Window resize should not be in input category"); 
		}

		while (m_Running)
		{
			// 清除颜色缓冲区
			glClearColor(1, 0, 1, 1);  // 设置清除颜色为紫色
			glClear(GL_COLOR_BUFFER_BIT);  // 清除颜色缓冲区

			// 更新所有层
			for (Layer* layer : m_LayerStack) {
				layer->OnUpdate();  // 调用每层更新逻辑
			}

			// 开始ImGui帧
			m_ImGuiLayer->Begin();

			// 渲染实现了OnImGuiRender()的层的ImGui UI
			for (Layer* layer : m_LayerStack) {
				layer->OnImGuiRender();  // 实现了绘制GUI的层会调用IMGUI渲染
			}

			// 结束ImGui帧并渲染
			m_ImGuiLayer->End();

			// 更新窗口（交换缓冲区等）
			m_Window->OnUpdate();
		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		m_Running = false;
		return true;
	}

} 