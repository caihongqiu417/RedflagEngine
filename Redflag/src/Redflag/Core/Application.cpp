#include "rfpch.h"
#include "Application.h"
#include "Redflag/Core/Log.h"


#include "Events/ApplicationEvent.h"
#include "Redflag/Assets/AssetManager.h"

#include"Input.h"
#include "Redflag/Renderer/GraphicsContext.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "Redflag/Renderer/Renderer.h"

#include"Redflag/Scripting/ScriptEngine.h"

#include"GLFW/glfw3.h"
#include"Core/Timestep.h"



namespace Redflag
{
	#define BIND_EVENT_FN(x) std::bind(&Application::x,this,std::placeholders::_1)

	Application* Application::s_Instance = nullptr;

	Application::Application(const std::string& name, bool visible)
		: Application(WindowProps(name, WindowProps::DefaultWidth, WindowProps::DefaultHeight, visible))
	{
	}

    Application::Application(const WindowProps& props)
    {
		RF_PROFILE_FUNCTION();

        // 单例检查：确保只有一个Application实例
        RF_CORE_ASSERT(!s_Instance, "Application already exists!");
        s_Instance = this;

        // 创建窗口并设置事件回调
        m_Window = Window::Create(props);
        m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));

		//目前实际调用Renderer2D::Init()
		if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL)
			Renderer::Init();

		//脚本系统
		ScriptEngine::Init();

        // 创建ImGui调试界面层
        m_ImGuiLayer = new ImGuiLayer();
        PushOverlay(m_ImGuiLayer);       
    }

	Application::~Application() 
	{
		RF_PROFILE_FUNCTION();
		
		m_LayerStack.Clear();
		ScriptEngine::Shutdown();
		AssetManager::ClearAll();
		AssetManager::GetLibrary().Clear();
		if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL)
			Renderer::Shutdown();
		delete m_Window;
		s_Instance = nullptr;
	}

	void Application::PushLayer(Layer* layer)
	{
		RF_PROFILE_FUNCTION();

		m_LayerStack.PushLayer(layer);
		layer->OnAttach();
	}

	void Application::PushOverlay(Layer* layer)
	{
		RF_PROFILE_FUNCTION();

		m_LayerStack.PushOverlay(layer);
		layer->OnAttach();
	}

	void Application::Close()
	{
		m_Running = false;
	}	

	void Application::OnEvent(Event& e)
	{
		RF_PROFILE_FUNCTION();

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(OnWindowClose));
		dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(OnWindowResize));
		RF_CORE_INFO("{0}", e.ToString());

		//从overlay从后往前传播事件
		for (auto it = m_LayerStack.end();it != m_LayerStack.begin();)
		{
			(*--it)->OnEvent(e);
			if (e.m_Handled)
				break;
		}
	}


	void Application::Run(unsigned frameLimit)
	{
		RF_PROFILE_FUNCTION();

		WindowResizeEvent e(1280, 720);
		if (e.IsInCategory(EventCategoryApplication)) {
			RF_CORE_TRACE("Window resize event triggered"); 
		}
		if (e.IsInCategory(EventCategoryInput)) {
			RF_CORE_ERROR("Event category check: Window resize should not be in input category"); 
		}

		while (m_Running)
		{
			RF_PROFILE_SCOPE("RunLoop");

			//计算每帧时间步长
			float time = (float)glfwGetTime();
			Timestep timestep = time - m_LastFrameTime;
			m_LastFrameTime = time;

			// 更新所有层
			if (!m_Minimized && m_Window->GetGraphicsContext().BeginFrame())
			{
				{
					RF_PROFILE_SCOPE("LayerStack OnUpdate"); 
					for (Layer* layer : m_LayerStack) {
						layer->OnUpdate(timestep);  // 调用每层更新逻辑
					}
				}

				// 开始ImGui帧
				m_ImGuiLayer->Begin();

				{
					RF_PROFILE_SCOPE("LayerStack OnImGuiRender");
					// 渲染实现了OnImGuiRender()的层的ImGui UI
					for (Layer* layer : m_LayerStack) {
						layer->OnImGuiRender();  // 实现了绘制GUI的层会调用IMGUI渲染
					}
				}

				// 结束ImGui帧并渲染
				m_ImGuiLayer->End();
				if (frameLimit && --frameLimit == 0)
					Close();
				
			}
	
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
		RF_PROFILE_FUNCTION();

		if (e.GetWidth() == 0 || e.GetHeight() == 0)
		{
			m_Minimized = true;
			return false;
		}
		m_Minimized = false;

		if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL)
			Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());

		return false;
	}

} 