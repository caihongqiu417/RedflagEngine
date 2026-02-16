#include"rfpch.h"
#include"Renderer2D.h"

#include"VertexArray.h"
#include"Shader.h"
#include"RenderCommand.h"


namespace Redflag
{
    // 渲染器2D内部存储 - 存放所有需要在多次渲染调用间共享的资源
    struct Renderer2DStorage
    {
        Ref<VertexArray> QuadVertexArray;
        Ref<Shader> FlatColorShader;
	};

	static Renderer2DStorage* s_Data = nullptr;


	void Renderer2D::Init()
	{
		s_Data = new Renderer2DStorage();

        // =============================渲染初始化正方形 =========================================
       //1.创建正方形的顶点数组VAO
       s_Data->QuadVertexArray = Redflag::VertexArray::Create();

        // 2. 创建正方形的顶点缓冲区VBO
        float squareVertices[5 * 4] = {
            // 位置                                       
            -0.5f, -0.5f, 0.0f,
             0.5f, -0.5f, 0.0f,
             0.5f,  0.5f, 0.0f,
            -0.5f,  0.5f, 0.0f,
        };

        Redflag::Ref<Redflag::VertexBuffer> squareVB;
        squareVB.reset(Redflag::VertexBuffer::Create(squareVertices, sizeof(squareVertices)));

        // 3.1 VBO配置layout
        Redflag::BufferLayout squareLayout = {
            {Redflag::ShaderDataType::Float3,"a_Position"},
        };
        squareVB->SetLayout(squareLayout);
        // 3.2 VBO绑定到当前VAO
        s_Data->QuadVertexArray->AddVertexBuffer(squareVB);

        // 4.1 创建IBO
        uint32_t squareIndices[] = {
           0, 1, 2,  
           2, 3, 0, 
        };

        Redflag::Ref<Redflag::IndexBuffer> squareIB;
        squareIB.reset(Redflag::IndexBuffer::Create(squareIndices, 6));

        // 4.2 IBO绑定到当前VAO
        s_Data->QuadVertexArray->SetIndexBuffer(squareIB);
        // ======================================================================================



        // ===============================正方形着色器 ===========================================
        s_Data->FlatColorShader = Redflag::Shader::Create("assets/shaders/FlatColor.glsl");
        // ======================================================================================
	}
	void Renderer2D::Shutdown()
	{
		delete s_Data;
	}
	void Renderer2D::BeginScene(const OrthographicCamera& camera)
	{
        s_Data->FlatColorShader->Bind();
        s_Data->FlatColorShader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
		s_Data->FlatColorShader->SetMat4("u_Transform", glm::mat4(1.0f));
	}
	void Renderer2D::EndScene()
	{
		
	}
	void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color)
	{
		DrawQuad({ position.x, position.y, 0.0f }, size, color);
	}

    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
    {
        s_Data->FlatColorShader->Bind();
        s_Data->FlatColorShader->SetFloat4("u_Color", color);

        s_Data->QuadVertexArray->Bind();
        RenderCommand::DrawIndexed(s_Data->QuadVertexArray);
	}
}