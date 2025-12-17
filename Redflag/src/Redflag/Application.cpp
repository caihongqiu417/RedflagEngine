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

		// ========== 新增：设置三角形渲染数据 ==========

	// 1. 生成并绑定顶点数组对象 (VAO)
	// VAO存储所有顶点属性配置，绑定后后续的顶点属性设置都会存储在这个VAO中
		glGenVertexArrays(1, &m_VertexArray);
		glBindVertexArray(m_VertexArray);

		// 2. 生成并绑定顶点缓冲区 (VBO)
		// VBO用于在GPU中存储顶点数据
		glGenBuffers(1, &m_VertexBuffer);
		glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);

		// 3. 定义三角形的三个顶点坐标 (每个顶点有x,y,z三个分量)
		float vertices[3 * 3] = {
			-0.5f, -0.5f, 0.0f,  // 左下角顶点
			 0.5f, -0.5f, 0.0f,  // 右下角顶点
			 0.0f,  0.5f, 0.0f   // 顶部顶点
		};

		// 4. 将顶点数据复制到GPU的缓冲区中
		// GL_STATIC_DRAW表示数据不会或几乎不会改变
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

		// 5. 设置顶点属性指针，告诉OpenGL如何解释顶点数据
		// 启用顶点属性位置0 (对应着色器中的layout(location = 0))
		glEnableVertexAttribArray(0);
		// 参数说明：
		// 0: 属性位置
		// 3: 每个顶点的分量数量 (x,y,z)
		// GL_FLOAT: 数据类型
		// GL_FALSE: 不需要归一化
		// 3 * sizeof(float): 步长 (顶点之间的字节偏移)
		// nullptr: 偏移量 (从缓冲区开头开始)
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

		// 6. 生成并绑定索引缓冲区 (IBO/EBO)
		// 索引缓冲区定义顶点绘制的顺序
		glGenBuffers(1, &m_IndexBuffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IndexBuffer);

		// 7. 定义索引数据 (绘制顺序：0→1→2)
		unsigned int indices[3] = { 0, 1, 2 };

		// 8. 将索引数据复制到GPU的缓冲区中
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
		// ============================================

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

			// ========== 新增：绘制三角形 ==========
			// 2. 绑定顶点数组对象
			glBindVertexArray(m_VertexArray);

			// 3. 使用索引绘制三角形
			// 参数说明：
			// GL_TRIANGLES: 绘制模式，绘制三角形
			// 3: 要绘制的索引数量
			// GL_UNSIGNED_INT: 索引的数据类型
			// nullptr: 不使用索引缓冲区偏移
			glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, nullptr);
			// ====================================


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