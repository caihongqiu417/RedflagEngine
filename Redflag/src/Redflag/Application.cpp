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

		// 创建ImGui调试界面层
		m_ImGuiLayer = new ImGuiLayer();
		PushOverlay(m_ImGuiLayer);

		// ========== 渲染初始化 ==========

		// 1. 创建并绑定顶点数组对象(VAO)
		// VAO是容器，管理所有顶点状态
		glGenVertexArrays(1, &m_VertexArray);
		glBindVertexArray(m_VertexArray);  // 开始配置这个VAO

		// 2. 创建顶点缓冲区(VBO)
		float vertices[] = {
			// 左下角：薄荷绿
			-0.5f, -0.5f, 0.0f,  0.47f, 0.87f, 0.68f, 1.0f,
			// 右下角：淡紫色
			 0.5f, -0.5f, 0.0f,  0.81f, 0.67f, 0.87f, 1.0f,
			// 顶部：浅橙色
			 0.0f,  0.5f, 0.0f,  0.98f, 0.80f, 0.47f, 1.0f,
		};
		m_VertexBuffer.reset(VertexBuffer::Create(vertices, sizeof(vertices)));

		// 3. 配置VAO的属性槽0
		// 告诉VAO："槽0的数据来自当前VBO，格式是每12字节3个float"
		BufferLayout layout = {
			{ShaderDataType::Float3,"a_Position"},
			{ShaderDataType::Float4,"a_Color"},
		};

		m_VertexBuffer->SetLayout(layout);

		// 设置顶点属性
		uint32_t index = 0;
		for (const auto& element : m_VertexBuffer->GetLayout())
		{
			glEnableVertexAttribArray(index);
			glVertexAttribPointer(
				index,
				element.GetComponentCount(),
				ShaderDataTypeToOpenGLBaseType(element.Type),
				element.Normalized ? GL_TRUE : GL_FALSE,
				layout.GetStride(),
				(const void*)element.Offset
			);
			index++;
		}


		// 4. 创建索引缓冲区(IBO)
		// 定义绘制顺序：顶点0→1→2组成三角形
		uint32_t indices[] = { 0, 1, 2 };
		m_IndexBuffer.reset(IndexBuffer::Create(indices, 3));

		// 5. 创建着色器程序
		// 顶点着色器：从VAO的槽0读取位置，传递给片段着色器
		std::string vertexSrc = R"(
        #version 330 core

        layout(location=0) in vec3 a_Position; 
		layout(location=1) in vec4 a_Color;	

        out vec3 v_Position;                    // 传递给片段着色器
		out vec4 v_Color;	
        void main()
        {
            v_Position = a_Position;
			v_Color=a_Color;
            gl_Position = vec4(a_Position, 1.0);
        }
    )";

		// 片段着色器：计算每个像素的颜色
		std::string fragmentSrc = R"(
        #version 330 core
        layout(location=0) out vec4 Fragcolor;
        in vec3 v_Position;                     // 接收来自顶点着色器的数据
		in vec4 v_Color;	
        void main()
        {
			Fragcolor=vec4(v_Color);
        }
    )";

		m_Shader.reset(new Shader(vertexSrc, fragmentSrc));
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
			glDrawElements(GL_TRIANGLES, m_IndexBuffer->GetCount(), GL_UNSIGNED_INT, nullptr);


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