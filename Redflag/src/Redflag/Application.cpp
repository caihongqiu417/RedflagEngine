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

		// ========== 设置三角形渲染数据 ==========

		// 1. 生成并绑定分配说明书VAO
		glGenVertexArrays(1, &m_VertexArray);
		glBindVertexArray(m_VertexArray);

		// 2. 生成并绑定顶点缓冲区VBO
		glGenBuffers(1, &m_VertexBuffer);
		glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);

		// 3. 定义顶点数据，写入VBO
		float vertices[3 * 3] = {
			-0.5f, -0.5f, 0.0f,  
			 0.5f, -0.5f, 0.0f,  
			 0.0f,  0.5f, 0.0f   
		};
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

		// 4. 设置VAO（告诉OpenGL如何解释顶点数据）
			// ========== CPU 端：向 0 号槽"写入"数据 ==========

			glEnableVertexAttribArray(0);          // ← 打开0号数据线

			// 告诉GPU："数据线0上的数据格式是这样的..."
			glVertexAttribPointer(
				0,                                  // ← 这是0号线
				3,                                  // 每个顶点有3个float（x,y,z）
				GL_FLOAT,                          // 数据类型是浮点数
				GL_FALSE,                          // 不进行归一化
				3 * sizeof(float),                 // 从当前顶点到下一个顶点的步长
				nullptr                            // 数据从开头开始
			);
			// ========== CPU 端：向 0 号槽"写入"数据 ==========

		// 5. 生成并绑定索引缓冲区IBO/EBO
		glGenBuffers(1, &m_IndexBuffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IndexBuffer);

		// 6. 定义索引数据：0→1→2，写入IBO
		unsigned int indices[3] = { 0, 1, 2 };
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
		// ============================================



		// ========== 写顶点和片段shader，编译链接成program==========
		//从0号线读取数据，赋值给a_Position变量
		std::string vertexSrc = R"(
			#version 330 core
			
			layout(location=0) in vec3 a_Position;
 
			out vec3 v_Position;

			void main()
			{
				v_Position=a_Position;
				gl_Position=vec4(a_Position,1.0);
			}

		)";

		std::string fragmentSrc = R"(
			#version 330 core

			layout(location=0) out vec4 Fragcolor;

			in vec3 v_Position;

			void main()
			{
				Fragcolor = vec4(v_Position*0.5+0.5,1.0);  
			}

		)";

		m_Shader.reset(new Shader(vertexSrc, fragmentSrc));

		// ====================================================

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
			// 1. 设置清屏颜色为深灰色并清除颜色缓冲区
			glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);

			//2. drawcall
			m_Shader->Bind();

			glBindVertexArray(m_VertexArray);
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, nullptr);


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