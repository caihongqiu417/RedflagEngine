#include"rfpch.h"
#include"Renderer2D.h"

#include"VertexArray.h"
#include"Shader.h"
#include"RenderCommand.h"

#include<glm/gtc/matrix_transform.hpp>


namespace Redflag
{
    // 渲染器2D内部存储 - 存放所有需要在多次渲染调用间共享的资源
    struct Renderer2DStorage
    {
        Ref<VertexArray> QuadVertexArray;

        Ref<Shader> TextureShader;

        Ref<Texture2D> WhiteTexture; // 用于绘制纯色方块的白色纹理
    };

    static Renderer2DStorage* s_Data = nullptr;


    void Renderer2D::Init()
    {
        RF_PROFILE_FUNCTION();

        s_Data = new Renderer2DStorage();

        // =============================渲染初始化正方形 =========================================
       //1.创建正方形的顶点数组VAO
        s_Data->QuadVertexArray = Redflag::VertexArray::Create();

        // 2. 创建正方形的顶点缓冲区VBO
        float squareVertices[5 * 4] = {
            // 位置                                       
            -0.5f, -0.5f, 0.0f,0.0f,0.0f,
             0.5f, -0.5f, 0.0f,1.0f,0.0f,
             0.5f,  0.5f, 0.0f,1.0f,1.0f,
            -0.5f,  0.5f, 0.0f,0.0f,1.0f,
        };

        Redflag::Ref<Redflag::VertexBuffer> squareVB;
        squareVB.reset(Redflag::VertexBuffer::Create(squareVertices, sizeof(squareVertices)));

        // 3.1 VBO配置layout
        Redflag::BufferLayout squareLayout = {
            {Redflag::ShaderDataType::Float3,"a_Position"},
            {Redflag::ShaderDataType::Float2,"a_TexCoord"},
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

        // 创建一个1x1的白色纹理，用于绘制纯色方块
        s_Data->WhiteTexture = Redflag::Texture2D::Create(1, 1);
        uint32_t whiteTextureData = 0xffffffff; // RGBA白色
        s_Data->WhiteTexture->SetData(&whiteTextureData, sizeof(whiteTextureData));

        //===============================纹理着色器 ===========================================
        s_Data->TextureShader = Redflag::Shader::Create("assets/shaders/Texture.glsl");
        s_Data->TextureShader->Bind();
        s_Data->TextureShader->SetInt("u_Texture", 0); // 这里把纹理单元0传递给着色器中的采样器变量u_Texture
        // ======================================================================================
    }
    void Renderer2D::Shutdown()
    {
        RF_PROFILE_FUNCTION();

        delete s_Data;
    }

    void Renderer2D::BeginScene(const OrthographicCamera& camera)
    {
        RF_PROFILE_FUNCTION();

        s_Data->TextureShader->Bind();
        s_Data->TextureShader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
    }

    void Renderer2D::EndScene()
    {
        RF_PROFILE_FUNCTION();
    }

    //=====================================================================================
    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color)
    {
        RF_PROFILE_FUNCTION();

        DrawQuad({ position.x, position.y, 0.0f }, size, color);
    }

    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
    {
        RF_PROFILE_FUNCTION();

        s_Data->TextureShader->SetFloat4("u_Color", color);
        s_Data->TextureShader->SetFloat("u_TilingFactor", 1.0f);

        //Todo:Bind white texture
        s_Data->WhiteTexture->Bind();

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        s_Data->TextureShader->SetMat4("u_Transform", transform);

        s_Data->QuadVertexArray->Bind();
        RenderCommand::DrawIndexed(s_Data->QuadVertexArray);
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>& texture, float tilingFactor,const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();

        DrawQuad({ position.x, position.y, 0.0f }, size, texture, tilingFactor,tintColor);
    }

    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture, float tilingFactor,const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();

        s_Data->TextureShader->SetFloat4("u_Color", tintColor);
        s_Data->TextureShader->SetFloat("u_TilingFactor", tilingFactor);
        // ★★★ 关键 ★★★
        texture->Bind();  // 这里把纹理上传/绑定到纹理单元0

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        s_Data->TextureShader->SetMat4("u_Transform", transform);

        s_Data->QuadVertexArray->Bind();
        RenderCommand::DrawIndexed(s_Data->QuadVertexArray);
    }
    //=====================================================================================

//===================================================================================== 
    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color)
    {
        RF_PROFILE_FUNCTION();
        DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, color);
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color)
    {
        RF_PROFILE_FUNCTION();

        s_Data->TextureShader->SetFloat4("u_Color", color);
        s_Data->TextureShader->SetFloat("u_TilingFactor", 1.0f);

        s_Data->WhiteTexture->Bind();

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
            * glm::rotate(glm::mat4(1.0f), glm::radians(rotation), { 0.0f, 0.0f, 1.0f })
            * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        s_Data->TextureShader->SetMat4("u_Transform", transform);
        s_Data->QuadVertexArray->Bind();
        RenderCommand::DrawIndexed(s_Data->QuadVertexArray);
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();
        DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, texture, tilingFactor,tintColor);
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tilingFactor,const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();

        s_Data->TextureShader->SetFloat4("u_Color", tintColor);
        s_Data->TextureShader->SetFloat("u_TilingFactor", tilingFactor);

        texture->Bind();

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
            * glm::rotate(glm::mat4(1.0f), glm::radians(rotation), { 0.0f, 0.0f, 1.0f })
            * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        s_Data->TextureShader->SetMat4("u_Transform", transform);
        s_Data->QuadVertexArray->Bind();
        RenderCommand::DrawIndexed(s_Data->QuadVertexArray);
    }
    //=====================================================================================
}