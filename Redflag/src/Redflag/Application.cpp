#include "rfpch.h"
#include "Application.h"
#include "Redflag/Log.h"


#include "Events/ApplicationEvent.h"
#include"glad/glad.h"

#include"Input.h"
#include "Redflag/Renderer/Renderer.h"

#include"GLFW/glfw3.h"
#include"Core/Timestep.h"



namespace Redflag
{
	#define BIND_EVENT_FN(x) std::bind(&Application::x,this,std::placeholders::_1)

	Application* Application::s_Instance = nullptr;

	static GLenum ShaderDataTypeToOpenGLBaseType(Redflag::ShaderDataType type)
	{
		switch (type)
		{
		case Redflag::ShaderDataType::Float:   return GL_FLOAT;
		case Redflag::ShaderDataType::Float2:  return GL_FLOAT;
		case Redflag::ShaderDataType::Float3:  return GL_FLOAT;
		case Redflag::ShaderDataType::Float4:  return GL_FLOAT;

		case Redflag::ShaderDataType::Mat3:    return GL_FLOAT;
		case Redflag::ShaderDataType::Mat4:    return GL_FLOAT;

		case Redflag::ShaderDataType::Int:     return GL_INT;
		case Redflag::ShaderDataType::Int2:    return GL_INT;
		case Redflag::ShaderDataType::Int3:    return GL_INT;
		case Redflag::ShaderDataType::Int4:    return GL_INT;

		case Redflag::ShaderDataType::Bool:    return GL_BOOL;

		default:
			// 错误处理：未知类型
			RF_CORE_ASSERT(false, "Unknown ShaderDataType!");
			return 0;
		}
	}



    Application::Application()
    {
        // 单例检查：确保只有一个Application实例
        RF_CORE_ASSERT(!s_Instance, "Application already exists!");
        s_Instance = this;

        // 创建窗口并设置事件回调
        m_Window = std::unique_ptr<Window>(Window::Create());
        m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));

		Renderer::Init();

        // 创建ImGui调试界面层
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
		dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(OnWindowResize));
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
			//计算每帧时间步长
			float time = (float)glfwGetTime();
			Timestep timestep = time - m_LastFrameTime;
			m_LastFrameTime = time;

			// 更新所有层
			if (!m_Minimized)
			{
				for (Layer* layer : m_LayerStack) {
					layer->OnUpdate(timestep);  // 调用每层更新逻辑
				}
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

	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		if (e.GetWidth() == 0 || e.GetHeight() == 0)
		{
			m_Minimized = true;
			return false;
		}
		m_Minimized = false;

		Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());

		return false;
	}

} 