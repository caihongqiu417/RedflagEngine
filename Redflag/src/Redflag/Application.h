#pragma once
#include "Core.h"

//test for event
#include"Events/Event.h"

#include<Redflag/Window.h>

#include"Redflag/LayerStack.h"

#include "Redflag/Events/ApplicationEvent.h" 

#include"Redflag/ImGui/ImGuiLayer.h"

#include"Renderer/Shader.h"

#include"Renderer/Buffer.h"
#include"Renderer/VertexArray.h"



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

		// ========== 三角形渲染相关成员变量 ==========
		std::shared_ptr<VertexArray> m_VertexArray;
		//std::shared_ptr<VertexBuffer> m_VertexBuffer;
		//std::shared_ptr <IndexBuffer> m_IndexBuffer;
		//=============================================
		
		// ========== 正方形渲染相关成员变量 ==========
		std::shared_ptr<VertexArray> m_SquareVA;
		//std::shared_ptr<VertexBuffer> m_SquareVB;
		//std::shared_ptr <IndexBuffer> m_SquareIB;
		//=============================================

			//=====Shader program:用于三角形 =============
		std::unique_ptr<Shader> m_Shader;
		//=============================================

		//=====Shader program:用于正方形（蓝色主题） =============
		std::unique_ptr<Shader> m_BlueShader;
		//=======================================================

	};

	Application* CreateApplication();
}

