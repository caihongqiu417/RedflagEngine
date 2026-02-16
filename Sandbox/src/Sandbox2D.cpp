#include "rfpch.h"

#include "Redflag.h"


#include "Platform/OpenGL/OpenGLShader.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Redflag/Renderer/Shader.h"

#include "Sandbox2D.h"


Sandbox2D::Sandbox2D()
    : Layer("Sandbox2D"), m_CameraController(1280.0f / 720.0f, true)
{
}

void Sandbox2D::OnAttach()
{
    // =============================渲染初始化正方形 =========================================
       //1.创建正方形的顶点数组VAO
    m_SquareVA = Redflag::VertexArray::Create();

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

    // 3.1 VBO配置layout（使用相同的布局）
    Redflag::BufferLayout squareLayout = {
        {Redflag::ShaderDataType::Float3,"a_Position"},
    };
    squareVB->SetLayout(squareLayout);
    // 3.2 VBO绑定到当前VAO
    m_SquareVA->AddVertexBuffer(squareVB);

    // 4.1 创建正方形的索引缓冲区IBO（两个三角形组成一个正方形）
    uint32_t squareIndices[] = {
       0, 1, 2,  // 第一个三角形：左下-右下-右上
       2, 3, 0,  // 第二个三角形：左下-右上-左上
    };

    Redflag::Ref<Redflag::IndexBuffer> squareIB;
    squareIB.reset(Redflag::IndexBuffer::Create(squareIndices, 6));

    // 4.2 IBO绑定到当前VAO
    m_SquareVA->SetIndexBuffer(squareIB);
    // ======================================================================================

    // ===============================正方形着色器 ===========================================
	m_FlatColorShader = Redflag::Shader::Create("assets/shaders/FlatColor.glsl");
     // ======================================================================================
}

void Sandbox2D::OnDetach()
{
}

void Sandbox2D::OnUpdate(Redflag::Timestep ts)
{
    //Update
	m_CameraController.OnUpdate(ts);


    //Render
    Redflag::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
	Redflag::RenderCommand::Clear();

	Redflag::Renderer::BeginScene(m_CameraController.GetCamera());

	std::dynamic_pointer_cast<Redflag::OpenGLShader>(m_FlatColorShader)->Bind();
	std::dynamic_pointer_cast<Redflag::OpenGLShader>(m_FlatColorShader)->UploadUniformFloat4("u_Color", m_SquareColor);

	Redflag::Renderer::Submit(m_FlatColorShader, m_SquareVA, glm::scale(glm::mat4(1.0f), glm::vec3(0.5f)));

	Redflag::Renderer::EndScene();
}

void Sandbox2D::OnImGuiRender()
{
	ImGui::Begin("Settings");
	ImGui::ColorEdit4("SquareColor", glm::value_ptr(m_SquareColor));
	ImGui::End();
}

void Sandbox2D::OnEvent(Redflag::Event& event)
{
	m_CameraController.OnEvent(event);
}
