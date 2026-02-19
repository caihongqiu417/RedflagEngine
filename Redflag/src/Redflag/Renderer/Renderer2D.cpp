#include"rfpch.h"
#include"Renderer2D.h"

#include"VertexArray.h"
#include"Shader.h"
#include"RenderCommand.h"

#include<glm/gtc/matrix_transform.hpp>


namespace Redflag
{
    struct QuadVertex
    {
        glm::vec3 Position;
        glm::vec4 Color;
        glm::vec2 TexCoord;
        // 其他属性，法线等，可以根据需要添加
    };

    // 渲染器2D内部存储 - 存放所有需要在多次渲染调用间共享的资源
    struct Renderer2DData
    {
		const uint32_t MaxQuads = 10000; 
		const uint32_t MaxVertices = MaxQuads * 4; 
		const uint32_t MaxIndices = MaxQuads * 6;

        Ref<VertexArray> QuadVertexArray;
		Ref<VertexBuffer> QuadVertexBuffer;
        Ref<Shader> TextureShader;
        Ref<Texture2D> WhiteTexture; // 用于绘制纯色方块的白色纹理

		uint32_t QuadIndexCount = 0; // 当前批次中已使用的索引数量
		QuadVertex* QuadVertexBufferBase = nullptr; // CPU端顶点数据的起始地址
		QuadVertex* QuadVertexBufferPtr = nullptr; // CPU端顶点数据的当前写入位置
    };


    static Renderer2DData s_Data;


    void Renderer2D::Init()
    {
        RF_PROFILE_FUNCTION();


        // =============================渲染初始化正方形 =========================================
       //1.创建正方形的顶点数组VAO
        s_Data.QuadVertexArray = Redflag::VertexArray::Create();

        // 2. 创建正方形的顶点缓冲区VBO
        s_Data.QuadVertexBuffer= VertexBuffer::Create(s_Data.MaxVertices*sizeof(QuadVertex));


        // 3. VBO配置layout并绑定
        Redflag::BufferLayout squareLayout = {
            {Redflag::ShaderDataType::Float3,"a_Position"},
			{Redflag::ShaderDataType::Float4,"a_Color"},
            {Redflag::ShaderDataType::Float2,"a_TexCoord"},
        };
        s_Data.QuadVertexBuffer->SetLayout(squareLayout);
        s_Data.QuadVertexArray->AddVertexBuffer(s_Data.QuadVertexBuffer);

        
		s_Data.QuadVertexBufferBase = new QuadVertex[s_Data.MaxVertices];

        // 4 创建IBO
        uint32_t* quadIndices = new uint32_t[s_Data.MaxIndices];

        uint32_t offset = 0;
        for (uint32_t i = 0; i < s_Data.MaxIndices; i += 6) {
            quadIndices[i + 0] = offset + 0;
            quadIndices[i + 1] = offset + 1;
            quadIndices[i + 2] = offset + 2;

            quadIndices[i + 3] = offset + 2;
            quadIndices[i + 4] = offset + 3;
            quadIndices[i + 5] = offset + 0;

            offset += 4;
        }

		Ref<IndexBuffer> quadIB = IndexBuffer::Create(quadIndices, s_Data.MaxIndices);
        s_Data.QuadVertexArray->SetIndexBuffer(quadIB);
		delete[] quadIndices;
        
        // ======================================================================================

        // 创建一个1x1的白色纹理，用于绘制纯色方块
        s_Data.WhiteTexture = Redflag::Texture2D::Create(1, 1);
        uint32_t whiteTextureData = 0xffffffff; // RGBA白色
        s_Data.WhiteTexture->SetData(&whiteTextureData, sizeof(whiteTextureData));

        //===============================纹理着色器 ===========================================
        s_Data.TextureShader = Redflag::Shader::Create("assets/shaders/Texture.glsl");
        s_Data.TextureShader->Bind();
        s_Data.TextureShader->SetInt("u_Texture", 0); // 这里把纹理单元0传递给着色器中的采样器变量u_Texture
        // ======================================================================================
    }
    void Renderer2D::Shutdown()
    {
        RF_PROFILE_FUNCTION();
    }

    void Renderer2D::BeginScene(const OrthographicCamera& camera)
    {
        RF_PROFILE_FUNCTION();

        s_Data.TextureShader->Bind();
        s_Data.TextureShader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());

		s_Data.QuadIndexCount = 0;
		s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;
    }

    void Renderer2D::EndScene()
    {
        RF_PROFILE_FUNCTION();

		uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.QuadVertexBufferPtr - (uint8_t*)s_Data.QuadVertexBufferBase);
		s_Data.QuadVertexBuffer->SetData(s_Data.QuadVertexBufferBase, dataSize);

        Flush();

    }

    void Renderer2D::Flush()
    {
		RenderCommand::DrawIndexed(s_Data.QuadVertexArray, s_Data.QuadIndexCount);
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

		//=============================== CPU端准备顶点数据 ===========================================
        s_Data.QuadVertexBufferPtr->Color = color;
		s_Data.QuadVertexBufferPtr->Position = position;
		s_Data.QuadVertexBufferPtr->TexCoord = { 0.0f, 0.0f }; // 默认纹理坐标
		s_Data.QuadVertexBufferPtr++;

		s_Data.QuadVertexBufferPtr->Color = color;
		s_Data.QuadVertexBufferPtr->Position = { position.x + size.x, position.y, position.z };
		s_Data.QuadVertexBufferPtr->TexCoord = { 1.0f, 0.0f };
		s_Data.QuadVertexBufferPtr++;

		s_Data.QuadVertexBufferPtr->Color = color;
		s_Data.QuadVertexBufferPtr->Position = { position.x + size.x, position.y + size.y, position.z };
		s_Data.QuadVertexBufferPtr->TexCoord = { 1.0f, 1.0f };
		s_Data.QuadVertexBufferPtr++;

		s_Data.QuadVertexBufferPtr->Color = color;
		s_Data.QuadVertexBufferPtr->Position = { position.x, position.y + size.y, position.z };
		s_Data.QuadVertexBufferPtr->TexCoord = { 0.0f, 1.0f };
		s_Data.QuadVertexBufferPtr++;

        s_Data.QuadIndexCount += 6;
		// ======================================================================================

        /*
        s_Data.TextureShader->SetFloat4("u_Color", color);
        s_Data.TextureShader->SetFloat("u_TilingFactor", 1.0f);

        //Todo:Bind white texture
        s_Data.WhiteTexture->Bind();

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        s_Data.TextureShader->SetMat4("u_Transform", transform);

        s_Data.QuadVertexArray->Bind();
        RenderCommand::DrawIndexed(s_Data.QuadVertexArray);
        */
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>& texture, float tilingFactor,const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();

        DrawQuad({ position.x, position.y, 0.0f }, size, texture, tilingFactor,tintColor);
    }

    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture, float tilingFactor,const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();

        s_Data.TextureShader->SetFloat4("u_Color", tintColor);
        s_Data.TextureShader->SetFloat("u_TilingFactor", tilingFactor);
        // ★★★ 关键 ★★★
        texture->Bind();  // 这里把纹理上传/绑定到纹理单元0

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        s_Data.TextureShader->SetMat4("u_Transform", transform);

        s_Data.QuadVertexArray->Bind();
        RenderCommand::DrawIndexed(s_Data.QuadVertexArray);
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

        s_Data.TextureShader->SetFloat4("u_Color", color);
        s_Data.TextureShader->SetFloat("u_TilingFactor", 1.0f);

        s_Data.WhiteTexture->Bind();

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
            * glm::rotate(glm::mat4(1.0f), glm::radians(rotation), { 0.0f, 0.0f, 1.0f })
            * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        s_Data.TextureShader->SetMat4("u_Transform", transform);
        s_Data.QuadVertexArray->Bind();
        RenderCommand::DrawIndexed(s_Data.QuadVertexArray);
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();
        DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, texture, tilingFactor,tintColor);
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tilingFactor,const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();

        s_Data.TextureShader->SetFloat4("u_Color", tintColor);
        s_Data.TextureShader->SetFloat("u_TilingFactor", tilingFactor);

        texture->Bind();

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
            * glm::rotate(glm::mat4(1.0f), glm::radians(rotation), { 0.0f, 0.0f, 1.0f })
            * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        s_Data.TextureShader->SetMat4("u_Transform", transform);
        s_Data.QuadVertexArray->Bind();
        RenderCommand::DrawIndexed(s_Data.QuadVertexArray);
    }
    //=====================================================================================
}