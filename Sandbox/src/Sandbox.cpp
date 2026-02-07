#include "rfpch.h"
#include "Redflag.h"

#include"Platform/OpenGL/OpenGLShader.h"

#include<glm/gtc/type_ptr.hpp>

#include"glm/gtc/matrix_transform.hpp"


class ExampleLayer : public Redflag::Layer
{
public:
    ExampleLayer()
        : Layer("Example"),m_Camera(-2.0f, 2.0f, -2.0f, 2.0f),m_CameraPosition(0.0f)
    {
        // =============================渲染初始化三角形 ========================================
        //1.创建顶点数组VAO
        m_VertexArray.reset(Redflag::VertexArray::Create());

        // 2. 创建顶点缓冲区VBO
        float vertices[] = {
            // 左下角：薄荷绿
            -0.5f, -0.5f, 0.0f,  0.47f, 0.87f, 0.68f, 1.0f,
            // 右下角：淡紫色
             0.5f, -0.5f, 0.0f,  0.81f, 0.67f, 0.87f, 1.0f,
             // 顶部：浅橙色
              0.0f,  0.5f, 0.0f,  0.98f, 0.80f, 0.47f, 1.0f,
        };

        Redflag::Ref<Redflag::VertexBuffer> vertexBuffer;
        vertexBuffer.reset(Redflag::VertexBuffer::Create(vertices, sizeof(vertices)));

        //3.1 VBO配置layout
        Redflag::BufferLayout layout = {
            {Redflag::ShaderDataType::Float3,"a_Position"},
            {Redflag::ShaderDataType::Float4,"a_Color"},
        };
        vertexBuffer->SetLayout(layout);
        //3.2 当前VBO绑定到对应VAO
        m_VertexArray->AddVertexBuffer(vertexBuffer);

        // 4.1 创建索引缓冲区IBO
        uint32_t indices[] = { 0, 1, 2 };

        Redflag::Ref<Redflag::IndexBuffer> indexBuffer;
        indexBuffer.reset(Redflag::IndexBuffer::Create(indices, 3));

        //4.2 当前IBO绑定到对应VAO
        m_VertexArray->SetIndexBuffer(indexBuffer);
        // ======================================================================================

        // =============================渲染初始化正方形 =========================================
        //1.创建正方形的顶点数组VAO
        m_SquareVA.reset(Redflag::VertexArray::Create());

        // 2. 创建正方形的顶点缓冲区VBO
        float squareVertices[5*4] = {
            // 位置               //纹理                           
            -0.5f, -0.5f, 0.0f,   0.0f,0.0f,        // 左下角
             0.5f, -0.5f, 0.0f,   1.0f,0.0f,      // 右下角
             0.5f,  0.5f, 0.0f,   1.0f,1.0f,      // 右上角
            -0.5f,  0.5f, 0.0f,   0.0f,1.0f,      // 左上角
        };

        Redflag::Ref<Redflag::VertexBuffer> squareVB;
        squareVB.reset(Redflag::VertexBuffer::Create(squareVertices, sizeof(squareVertices)));

        // 3.1 VBO配置layout（使用相同的布局）
        Redflag::BufferLayout squareLayout = {
            {Redflag::ShaderDataType::Float3,"a_Position"},
			{Redflag::ShaderDataType::Float2,"a_TexCoord"},
        };
        squareVB->SetLayout(squareLayout);
        // 3.2 VBO绑定到当前VAO
        m_SquareVA->AddVertexBuffer(squareVB);

        // 4.1 创建正方形的索引缓冲区IBO（两个三角形组成一个正方形）
        uint32_t squareIndices[] = {
           0, 1, 2,  // 第一个三角形：左下-右下-右上
           0, 2, 3   // 第二个三角形：左下-右上-左上
        };

        Redflag::Ref<Redflag::IndexBuffer> squareIB;
        squareIB.reset(Redflag::IndexBuffer::Create(squareIndices, 6));

        // 4.2 IBO绑定到当前VAO
        m_SquareVA->SetIndexBuffer(squareIB);
        // ======================================================================================

        // ========== 创建第一个着色器：用于三角形 ==========
        std::string vertexSrc = R"(
        #version 330 core

        layout(location=0) in vec3 a_Position; 
        layout(location=1) in vec4 a_Color;	

        uniform mat4 u_Transform;
        uniform mat4 u_ViewProjection;

        out vec3 v_Position;                    // 传递给片段着色器
        out vec4 v_Color;	
        void main()
        {
            v_Position = a_Position;
            v_Color = a_Color;
            gl_Position = u_ViewProjection*u_Transform*vec4(a_Position, 1.0);
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

        m_Shader.reset(Redflag::Shader::Create(vertexSrc, fragmentSrc));

        // ========== 创建第二个着色器：用于正方形 ==========

        std::string vertexSrc2 = R"(
                #version 330 core

                layout(location=0) in vec3 a_Position;

                uniform mat4 u_Transform;
                uniform mat4 u_ViewProjection;


                out vec3 v_Position;
    
              void main()
              {
                    v_Position = a_Position;
                    gl_Position = u_ViewProjection*u_Transform*vec4(a_Position, 1.0);
             }
        )";

        std::string fragmentSrc2 = R"(
             #version 330 core
                layout(location=0) out vec4 Fragcolor;
                in vec3 v_Position;

                uniform vec3 u_Color;
               
                void main()
                {
                   Fragcolor =vec4(u_Color,1.0f);
                }
        )";

        m_FlatShader.reset(Redflag::Shader::Create(vertexSrc2, fragmentSrc2));

        // ========== 创建第三个着色器：用于正方形 ==========
		m_TextureShader.reset(Redflag::Shader::Create("assets/shaders/Texture.glsl"));

		m_Texture = Redflag::Texture2D::Create("assets/textures/Checkerboard.png");
		m_RedCloudTexture = Redflag::Texture2D::Create("assets/textures/RedCloud.png");

		std::dynamic_pointer_cast<Redflag::OpenGLShader>(m_TextureShader)->Bind();
		std::dynamic_pointer_cast<Redflag::OpenGLShader>(m_TextureShader)->UploadUniformInt("u_Texture", 0);

    }


    void OnUpdate(Redflag::Timestep ts) override
    {
        //根据输入实现相机移动和旋转
        if (Redflag::Input::IsKeyPressed(RF_KEY_LEFT))
            m_CameraPosition.x += m_CameraMoveSpeed*ts;
        if (Redflag::Input::IsKeyPressed(RF_KEY_RIGHT))
            m_CameraPosition.x -= m_CameraMoveSpeed * ts;
        if (Redflag::Input::IsKeyPressed(RF_KEY_UP))
            m_CameraPosition.y -= m_CameraMoveSpeed * ts;
        if (Redflag::Input::IsKeyPressed(RF_KEY_DOWN))
            m_CameraPosition.y += m_CameraMoveSpeed * ts;

        if (Redflag::Input::IsKeyPressed(RF_KEY_A))
            m_CameraRotation -= m_CameraRotationSpeed * ts;
        if (Redflag::Input::IsKeyPressed(RF_KEY_D))
            m_CameraRotation += m_CameraRotationSpeed * ts;

        //设置清屏颜色为深灰色并清除颜色缓冲区
        Redflag::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
        Redflag::RenderCommand::Clear();

        m_Camera.SetPosition(m_CameraPosition);
        m_Camera.SetRotation(m_CameraRotation);

        Redflag::Renderer::BeginScene(m_Camera);

        // ========== 绘制正方形网格（使用第二个着色器） ==========
        glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));

		std::dynamic_pointer_cast<Redflag::OpenGLShader>(m_FlatShader)->Bind();
		std::dynamic_pointer_cast<Redflag::OpenGLShader>(m_FlatShader)->UploadUniformFloat3("u_Color", m_SquareColor);

        for (int y = 0; y < 20; y++)
        {
            for (int x = 0; x < 20; x++)
            {
                glm::vec3 pos(x * 0.11f - 1.0f, y * 0.11f - 1.0f, 0.0f);
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos) * scale;
                Redflag::Renderer::Submit(m_FlatShader, m_SquareVA, transform);
            }
        }
        

        // ========== 绘制正方形（使用第三个着色器） ==========
        m_Texture->Bind();

		Redflag::Renderer::Submit(m_TextureShader, m_SquareVA, glm::scale(glm::mat4(1.0f), glm::vec3(1.5f)));

        m_RedCloudTexture->Bind();

        Redflag::Renderer::Submit(m_TextureShader, m_SquareVA, 
            glm::translate(glm::mat4(1.0f),glm::vec3(0.25f,-0.25f,0.0f))*glm::scale(glm::mat4(1.0f), glm::vec3(1.5f)));

        // ========== 绘制三角形（使用第一个着色器） ==========
        //Redflag::Renderer::Submit(m_Shader, m_VertexArray);
  

        Redflag::Renderer::EndScene();
    }


    void OnEvent(Redflag::Event& event) override
    {

    }

    void OnImGuiRender() override
    {
	    ImGui::Begin("Settings");
		ImGui::ColorEdit3("Square Color", glm::value_ptr(m_SquareColor));
        ImGui::End();
	}


private:
    // ========== 三角形渲染相关成员变量 ==========
    Redflag::Ref<Redflag::VertexArray> m_VertexArray;
    //=============================================

    // ========== 正方形渲染相关成员变量 ==========
    Redflag::Ref<Redflag::VertexArray> m_SquareVA;
    glm::vec3 m_SquareColor = { 0.4f, 0.8f, 0.6f };
    Redflag::Ref<Redflag::Texture2D> m_Texture;
    Redflag::Ref<Redflag::Texture2D> m_RedCloudTexture;
    //=============================================

    //=====Shader program:用于三角形 =============
    Redflag::Ref<Redflag::Shader> m_Shader;
    //=============================================

    //=====Shader program:用于正方形 =============
    Redflag::Ref<Redflag::Shader> m_FlatShader,m_TextureShader;
    //============================================

    Redflag::OrthographicCamera m_Camera;
    glm::vec3 m_CameraPosition;
    float m_CameraMoveSpeed = 3.0f;
    float m_CameraRotation = 0.0f;
    float m_CameraRotationSpeed = 50.0f;
};

class Sandbox : public Redflag::Application
{
public:
    Sandbox()
    {
        PushLayer(new ExampleLayer());
    }

    ~Sandbox()
    {
    }
};

Redflag::Application* Redflag::CreateApplication()
{
    return new Sandbox();
}