#include "rfpch.h"
#include "Application.h"
#include "Redflag/Log.h"


#include "Events/ApplicationEvent.h"
#include"glad/glad.h"

#include"Input.h"
#include "Redflag/Renderer/Renderer.h"



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


        // =============================渲染初始化三角形 ========================================
        //1.创建顶点数组VAO
        m_VertexArray.reset(VertexArray::Create());

        // 2. 创建顶点缓冲区VBO
        float vertices[] = {
            // 左下角：薄荷绿
            -0.5f, -0.5f, 0.0f,  0.47f, 0.87f, 0.68f, 1.0f,
            // 右下角：淡紫色
             0.5f, -0.5f, 0.0f,  0.81f, 0.67f, 0.87f, 1.0f,
             // 顶部：浅橙色
              0.0f,  0.5f, 0.0f,  0.98f, 0.80f, 0.47f, 1.0f,
        };

        std::shared_ptr<VertexBuffer> vertexBuffer;
        vertexBuffer.reset(VertexBuffer::Create(vertices, sizeof(vertices)));

        //3.1 VBO配置layout
        BufferLayout layout = {
            {ShaderDataType::Float3,"a_Position"},
            {ShaderDataType::Float4,"a_Color"},
        };
        vertexBuffer->SetLayout(layout);
        //3.2 当前VBO绑定到对应VAO
        m_VertexArray->AddVertexBuffer(vertexBuffer);

        // 4.1 创建索引缓冲区IBO
        uint32_t indices[] = { 0, 1, 2 };

        std::shared_ptr<IndexBuffer> indexBuffer;
        indexBuffer.reset(IndexBuffer::Create(indices, 3));

        //4.2 当前IBO绑定到对应VAO
        m_VertexArray->SetIndexBuffer(indexBuffer);
        // ======================================================================================

        // =============================渲染初始化正方形 =========================================
        //1.创建正方形的顶点数组VAO
        m_SquareVA.reset(VertexArray::Create());

        // 2. 创建正方形的顶点缓冲区VBO
        float squareVertices[] = {
            // 位置: (x, y, z)           // 颜色: (r, g, b, a)
            // 左下角：深蓝色渐变到浅蓝色
            -0.5f, -0.5f, 0.0f,          0.1f, 0.2f, 0.8f, 1.0f,   // 深蓝色
            // 右下角：紫色渐变到蓝色
             0.5f, -0.5f, 0.0f,          0.6f, 0.2f, 1.0f, 1.0f,   // 紫色
             // 右上角：浅蓝色渐变到白色
              0.5f,  0.5f, 0.0f,          0.3f, 0.6f, 1.0f, 1.0f,   // 浅蓝色
              // 左上角：青色渐变到蓝色
              -0.5f,  0.5f, 0.0f,          0.0f, 0.8f, 0.9f, 1.0f,   // 青色
        };

        std::shared_ptr<VertexBuffer> squareVB;
        squareVB.reset(VertexBuffer::Create(squareVertices, sizeof(squareVertices)));

        // 3.1 VBO配置layout（使用相同的布局）
        BufferLayout squareLayout = {
            {ShaderDataType::Float3,"a_Position"},
            {ShaderDataType::Float4,"a_Color"},
        };
        squareVB->SetLayout(squareLayout);
        // 3.2 VBO绑定到当前VAO
        m_SquareVA->AddVertexBuffer(squareVB);

        // 4.1 创建正方形的索引缓冲区IBO（两个三角形组成一个正方形）
        uint32_t squareIndices[] = {
            0, 1, 2,  // 第一个三角形：左下-右下-右上
            2, 3, 0   // 第二个三角形：右上-左上-左下
        };

        std::shared_ptr<IndexBuffer> squareIB;
        squareIB.reset(IndexBuffer::Create(squareIndices, 6));

        // 4.2 IBO绑定到当前VAO
        m_SquareVA->SetIndexBuffer(squareIB);
        // ======================================================================================

        // ========== 创建第一个着色器：用于三角形 ==========
        std::string vertexSrc = R"(
        #version 330 core

        layout(location=0) in vec3 a_Position; 
        layout(location=1) in vec4 a_Color;	

        out vec3 v_Position;                    // 传递给片段着色器
        out vec4 v_Color;	
        void main()
        {
            v_Position = a_Position;
            v_Color = a_Color;
            gl_Position = vec4(a_Position, 1.0);
        }
    )";

        std::string fragmentSrc = R"(
        #version 330 core
        layout(location=0) out vec4 Fragcolor;
        in vec3 v_Position;                     // 接收来自顶点着色器的数据
        in vec4 v_Color;	
        void main()
        {
            Fragcolor = vec4(v_Color);
        }
    )";

        m_Shader.reset(new Shader(vertexSrc, fragmentSrc));

        // ========== 创建第二个着色器：用于正方形 ==========

        std::string vertexSrc2 = R"(
             #version 330 core

                layout(location=0) in vec3 a_Position;
             layout(location=1) in vec4 a_Color;

                out vec3 v_Position;
                out vec4 v_Color;
    
              void main()
              {
                    v_Position = a_Position;
                     v_Color = a_Color;
                    gl_Position = vec4(a_Position, 1.0);
             }
        )";

        std::string fragmentSrc2 = R"(
             #version 330 core
                 layout(location=0) out vec4 Fragcolor;
                in vec3 v_Position;
                in vec4 v_Color;
    
                void main()
                {
                     // 轻微调亮效果：乘以1.2
                        Fragcolor = vec4(v_Color.rgb * 1.2, v_Color.a);
                }
        )";

        m_BlueShader.reset(new Shader(vertexSrc2, fragmentSrc2));
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

			//设置清屏颜色为深灰色并清除颜色缓冲区
            RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
            RenderCommand::Clear();

            Renderer::BeginScene();

            // ========== 绘制正方形（使用第二个着色器） ==========
            m_BlueShader->Bind();
            Renderer::Submit(m_SquareVA);
            glDrawElements(GL_TRIANGLES, m_SquareVA->GetIndexBuffer()->GetCount(), GL_UNSIGNED_INT, nullptr);

			// ========== 绘制三角形（使用第一个着色器） ==========
			m_Shader->Bind();
            Renderer::Submit(m_VertexArray);
			glDrawElements(GL_TRIANGLES, m_VertexArray->GetIndexBuffer()->GetCount(), GL_UNSIGNED_INT, nullptr);

            Renderer::EndScene();

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