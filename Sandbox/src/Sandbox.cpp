#include "rfpch.h"
#include "Redflag.h"

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

        std::shared_ptr<Redflag::VertexBuffer> vertexBuffer;
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

        std::shared_ptr<Redflag::IndexBuffer> indexBuffer;
        indexBuffer.reset(Redflag::IndexBuffer::Create(indices, 3));

        //4.2 当前IBO绑定到对应VAO
        m_VertexArray->SetIndexBuffer(indexBuffer);
        // ======================================================================================

        // =============================渲染初始化正方形 =========================================
        //1.创建正方形的顶点数组VAO
        m_SquareVA.reset(Redflag::VertexArray::Create());

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

        std::shared_ptr<Redflag::VertexBuffer> squareVB;
        squareVB.reset(Redflag::VertexBuffer::Create(squareVertices, sizeof(squareVertices)));

        // 3.1 VBO配置layout（使用相同的布局）
        Redflag::BufferLayout squareLayout = {
            {Redflag::ShaderDataType::Float3,"a_Position"},
            {Redflag::ShaderDataType::Float4,"a_Color"},
        };
        squareVB->SetLayout(squareLayout);
        // 3.2 VBO绑定到当前VAO
        m_SquareVA->AddVertexBuffer(squareVB);

        // 4.1 创建正方形的索引缓冲区IBO（两个三角形组成一个正方形）
        uint32_t squareIndices[] = {
            0, 1, 2,  // 第一个三角形：左下-右下-右上
            2, 3, 0   // 第二个三角形：右上-左上-左下
        };

        std::shared_ptr<Redflag::IndexBuffer> squareIB;
        squareIB.reset(Redflag::IndexBuffer::Create(squareIndices, 6));

        // 4.2 IBO绑定到当前VAO
        m_SquareVA->SetIndexBuffer(squareIB);
        // ======================================================================================

        // ========== 创建第一个着色器：用于三角形 ==========
        std::string vertexSrc = R"(
        #version 330 core

        layout(location=0) in vec3 a_Position; 
        layout(location=1) in vec4 a_Color;	

        uniform mat4 u_ViewProjection;

        out vec3 v_Position;                    // 传递给片段着色器
        out vec4 v_Color;	
        void main()
        {
            v_Position = a_Position;
            v_Color = a_Color;
            gl_Position = u_ViewProjection*vec4(a_Position, 1.0);
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

        m_Shader.reset(new Redflag::Shader(vertexSrc, fragmentSrc));

        // ========== 创建第二个着色器：用于正方形 ==========

        std::string vertexSrc2 = R"(
             #version 330 core

                layout(location=0) in vec3 a_Position;
                layout(location=1) in vec4 a_Color;

                uniform mat4 u_ViewProjection;

                out vec3 v_Position;
                out vec4 v_Color;
    
              void main()
              {
                    v_Position = a_Position;
                     v_Color = a_Color;
                    gl_Position = u_ViewProjection*vec4(a_Position, 1.0);
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

        m_BlueShader.reset(new Redflag::Shader(vertexSrc2, fragmentSrc2));
    }

    void OnUpdate(Redflag::Timestep ts) override
    {
        //根据输入实现相机移动和旋转
        if (Redflag::Input::IsKeyPressed(RF_KEY_LEFT))
            m_CameraPosition.x -= m_CameraMoveSpeed*ts;
        if (Redflag::Input::IsKeyPressed(RF_KEY_RIGHT))
            m_CameraPosition.x += m_CameraMoveSpeed * ts;
        if (Redflag::Input::IsKeyPressed(RF_KEY_UP))
            m_CameraPosition.y += m_CameraMoveSpeed * ts;
        if (Redflag::Input::IsKeyPressed(RF_KEY_DOWN))
            m_CameraPosition.y -= m_CameraMoveSpeed * ts;

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

        // ========== 绘制正方形（使用第二个着色器） ==========
        Redflag::Renderer::Submit(m_BlueShader, m_SquareVA);

        // ========== 绘制三角形（使用第一个着色器） ==========
        Redflag::Renderer::Submit(m_Shader, m_VertexArray);
  

        Redflag::Renderer::EndScene();
    }

   

    void OnEvent(Redflag::Event& event) override
    {


    }




private:
    // ========== 三角形渲染相关成员变量 ==========
    std::shared_ptr<Redflag::VertexArray> m_VertexArray;
    //=============================================

    // ========== 正方形渲染相关成员变量 ==========
    std::shared_ptr<Redflag::VertexArray> m_SquareVA;
    //=============================================

    //=====Shader program:用于三角形 =============
    std::shared_ptr<Redflag::Shader> m_Shader;
    //=============================================

    //=====Shader program:用于正方形（蓝色主题） =============
    std::shared_ptr<Redflag::Shader> m_BlueShader;
    //=======================================================

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