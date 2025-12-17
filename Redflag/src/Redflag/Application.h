#pragma once
#include "Core.h"

//test for event
#include"Events/Event.h"

#include<Redflag/Window.h>

#include"Redflag/LayerStack.h"

#include"Redflag/Events/Event.h"
#include "Redflag/Events/ApplicationEvent.h" 

#include"Redflag/ImGui/ImGuiLayer.h"



namespace Redflag
{
	class REDFLAG_API Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();

		void OnEvent(Event& e);

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* overlay);

		inline static Application& Get() { return *s_Instance; }
		inline Window& GetWindow() { return *m_Window; }
		bool OnWindowClose(WindowCloseEvent& e);
	
	private:
		std::unique_ptr<Window>  m_Window;
		ImGuiLayer* m_ImGuiLayer;
		bool m_Running = true;
		LayerStack m_LayerStack;
	private:
		static Application* s_Instance;
		// ========== 新增：三角形渲染相关成员变量 ==========
		unsigned int m_VertexArray;   // 顶点数组对象 (VAO)
		unsigned int m_VertexBuffer;  // 顶点缓冲区对象 (VBO)
		unsigned int m_IndexBuffer;   // 索引缓冲区对象 (IBO/EBO)
		// ============================================

	};

	Application* CreateApplication();
}

