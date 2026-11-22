#pragma once
#include "Core.h"
#include "JobSystem.h"


#include"Events/Event.h"

#include<Redflag/Core/Window.h>

#include"Redflag/Core/LayerStack.h"

#include "Redflag/Events/ApplicationEvent.h" 

#include"Redflag/ImGui/ImGuiLayer.h"

#include"Renderer/Shader.h"

#include"Renderer/Buffer.h"
#include"Renderer/VertexArray.h"


#include"Redflag/Renderer/OrthographicCamera.h"

#include"Redflag/Core/Timestep.h"



namespace Redflag
{
	class REDFLAG_API Application
	{
	public:
		Application(const std::string& name="Redflag App", bool visible = true);
		explicit Application(const WindowProps& props);
		virtual ~Application();

		void Run(unsigned frameLimit = 0);

		void OnEvent(Event& e);

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* overlay);

		inline static Application& Get() { return *s_Instance; }
		static Application* Current() { return s_Instance; }
		JobSystem& GetJobs();
		JobSystem* ExistingJobs() const { return m_Jobs.get(); }
		inline Window& GetWindow() { return *m_Window; }

		inline ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; }

		void Close();

		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowResize(WindowResizeEvent& e);
	
	private:
		Window* m_Window;
		Scope<JobSystem> m_Jobs;
		ImGuiLayer* m_ImGuiLayer;
		bool m_Running = true;
		bool m_Minimized = false;
		LayerStack m_LayerStack;
		float m_LastFrameTime = 0.0f;
	private:
		static Application* s_Instance;

		
	};

	Application* CreateApplication();
}

