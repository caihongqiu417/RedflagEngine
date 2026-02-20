#include "rfpch.h"
#include "Renderer2D.h"

#include "VertexArray.h"
#include "Shader.h"
#include "RenderCommand.h"

#include <glm/gtc/matrix_transform.hpp>

//==============================================================================
// 2D批处理渲染器实现
// 功能：支持批量绘制四边形（带纹理或不带纹理），优化渲染性能
// 核心机制：CPU端累积顶点数据，一次上传GPU，一次绘制调用渲染多个四边形
//==============================================================================

namespace Redflag
{
    //--------------------------------------------------------------------------
    // 顶点结构体：描述单个顶点的所有属性
    //--------------------------------------------------------------------------
    struct QuadVertex
    {
        glm::vec3 Position;      // 顶点位置（模型空间坐标，经变换后为世界坐标）
        glm::vec4 Color;         // 顶点颜色（RGBA）
        glm::vec2 TexCoord;      // 纹理坐标（UV，范围0~1）
        float textureIndex;          // 纹理索引（0=白色纹理，1~31=其他纹理）
    };

    //--------------------------------------------------------------------------
    // 渲染器内部数据：存储所有渲染状态和资源（单例模式）
    //--------------------------------------------------------------------------
    struct Renderer2DData
    {
        //========== 容量限制 ==========
        const uint32_t MaxQuads = 10000;                    // 单批次最大四边形数量
        const uint32_t MaxVertices = MaxQuads * 4;          // 最大顶点数（4顶点/四边形）
        const uint32_t MaxIndices = MaxQuads * 6;           // 最大索引数（6索引/四边形）
        static const uint32_t MaxTextureSlots = 32;         // 最大纹理槽数量（硬件限制）

        //========== GPU资源 ==========
        Ref<VertexArray> QuadVertexArray;                    // 顶点数组对象（VAO）
        Ref<VertexBuffer> QuadVertexBuffer;                  // 顶点缓冲区（VBO）
        Ref<Shader> TextureShader;                            // 纹理着色器
        Ref<Texture2D> WhiteTexture;                          // 1x1白色纹理（用于纯色四边形）

        //========== 批处理状态 ==========
        uint32_t QuadIndexCount = 0;                          // 当前批次已使用的索引数量
        QuadVertex* QuadVertexBufferBase = nullptr;           // CPU端顶点缓冲区基址
        QuadVertex* QuadVertexBufferPtr = nullptr;            // CPU端顶点缓冲区当前写入指针

        //========== 纹理管理 ==========
        std::array<Ref<Texture2D>, MaxTextureSlots> TextureSlots;   // 当前批次使用的纹理数组
        uint32_t TextureSlotIndex = 1;                               // 下一个可用纹理槽索引（0预留给白色纹理）

        //========== 几何数据 ==========
        glm::vec4 QuadVertexPositions[4];                    // 四边形标准顶点位置（中心为原点，范围-0.5~0.5）
    };

    // 全局静态渲染数据实例
    static Renderer2DData s_Data;

    //==========================================================================
    // 初始化：程序启动时调用一次，设置所有GPU资源和初始状态
    //==========================================================================
    void Renderer2D::Init()
    {
        RF_PROFILE_FUNCTION();

        //---------- 1. 创建顶点数组对象 ----------
        s_Data.QuadVertexArray = Redflag::VertexArray::Create();

        //---------- 2. 创建顶点缓冲区 ----------
        // 预留最大顶点数的内存空间
        s_Data.QuadVertexBuffer = VertexBuffer::Create(s_Data.MaxVertices * sizeof(QuadVertex));

        //---------- 3. 设置顶点布局 ----------
        // 告诉GPU每个属性的含义、类型和位置
        Redflag::BufferLayout squareLayout = {
            {Redflag::ShaderDataType::Float3, "a_Position"},  // 位置：3个float
            {Redflag::ShaderDataType::Float4, "a_Color"},     // 颜色：4个float
            {Redflag::ShaderDataType::Float2, "a_TexCoord"},  // 纹理坐标：2个float
            {Redflag::ShaderDataType::Float, "a_textureIndex"}    // 纹理索引：1个float
        };
        s_Data.QuadVertexBuffer->SetLayout(squareLayout);
        s_Data.QuadVertexArray->AddVertexBuffer(s_Data.QuadVertexBuffer);

        //---------- 4. 分配CPU端顶点缓冲区内存 ----------
        // 用于临时存储每帧的顶点数据
        s_Data.QuadVertexBufferBase = new QuadVertex[s_Data.MaxVertices];

        //---------- 5. 创建索引缓冲区 ----------
        uint32_t* quadIndices = new uint32_t[s_Data.MaxIndices];
        for (uint32_t i = 0; i < s_Data.MaxQuads; i++)
        {
            // 每个四边形由2个三角形组成，索引顺序：0-1-2-2-3-0
            quadIndices[i * 6 + 0] = i * 4 + 0;  // 三角形1: 顶点0
            quadIndices[i * 6 + 1] = i * 4 + 1;  // 三角形1: 顶点1
            quadIndices[i * 6 + 2] = i * 4 + 2;  // 三角形1: 顶点2
            quadIndices[i * 6 + 3] = i * 4 + 2;  // 三角形2: 顶点2
            quadIndices[i * 6 + 4] = i * 4 + 3;  // 三角形2: 顶点3
            quadIndices[i * 6 + 5] = i * 4 + 0;  // 三角形2: 顶点0
        }
        Ref<IndexBuffer> quadIB = IndexBuffer::Create(quadIndices, s_Data.MaxIndices);
        s_Data.QuadVertexArray->SetIndexBuffer(quadIB);
        delete[] quadIndices;

        //---------- 6. 创建白色纹理 ----------
        // 用于绘制纯色四边形（通过顶点颜色着色）
        s_Data.WhiteTexture = Redflag::Texture2D::Create(1, 1);
        uint32_t whiteTextureData = 0xffffffff;  // RGBA白色（不透明）
        s_Data.WhiteTexture->SetData(&whiteTextureData, sizeof(whiteTextureData));

        //---------- 7. 准备纹理采样器数组 ----------
        // 告诉着色器每个采样器对应的纹理单元编号
        int32_t samplers[s_Data.MaxTextureSlots];
        for (uint32_t i = 0; i < s_Data.MaxTextureSlots; i++)
            samplers[i] = i;  // 采样器i对应纹理单元i

        //---------- 8. 创建并配置纹理着色器 ----------
        s_Data.TextureShader = Redflag::Shader::Create("assets/shaders/Texture.glsl");
        s_Data.TextureShader->Bind();
        s_Data.TextureShader->SetIntArray("u_Textures", samplers, s_Data.MaxTextureSlots);

        //---------- 9. 初始化纹理槽 ----------
        // 0号槽永久绑定白色纹理
        s_Data.TextureSlots[0] = s_Data.WhiteTexture;

        //---------- 10. 初始化标准四边形顶点位置 ----------
        // 以中心为原点，范围-0.5~0.5，便于变换
        s_Data.QuadVertexPositions[0] = { -0.5f, -0.5f, 0.0f, 1.0f };  // 左下
        s_Data.QuadVertexPositions[1] = { 0.5f, -0.5f, 0.0f, 1.0f };   // 右下
        s_Data.QuadVertexPositions[2] = { 0.5f, 0.5f, 0.0f, 1.0f };    // 右上
        s_Data.QuadVertexPositions[3] = { -0.5f, 0.5f, 0.0f, 1.0f };   // 左上
    }

    //==========================================================================
    // 关闭：程序结束时调用，释放资源
    //==========================================================================
    void Renderer2D::Shutdown()
    {
        RF_PROFILE_FUNCTION();
        // TODO: 释放CPU端分配的缓冲区内存
        // delete[] s_Data.QuadVertexBufferBase;
    }

    //==========================================================================
    // 开始新批次：每帧开始时调用，重置批处理状态
    //==========================================================================
    void Renderer2D::BeginScene(const OrthographicCamera& camera)
    {
        RF_PROFILE_FUNCTION();

        // 绑定着色器并设置视图投影矩阵
        s_Data.TextureShader->Bind();
        s_Data.TextureShader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());

        // 重置批处理状态
        s_Data.QuadIndexCount = 0;                           // 清空索引计数
        s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;  // 重置写入指针到缓冲区开头
        s_Data.TextureSlotIndex = 1;                          // 重置纹理槽索引（0已用）
    }

    //==========================================================================
    // 结束当前批次：上传顶点数据到GPU并执行绘制
    //==========================================================================
    void Renderer2D::EndScene()
    {
        RF_PROFILE_FUNCTION();

        // 计算实际使用的数据大小
        uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.QuadVertexBufferPtr -
            (uint8_t*)s_Data.QuadVertexBufferBase);

        // 将CPU端累积的顶点数据上传到GPU
        s_Data.QuadVertexBuffer->SetData(s_Data.QuadVertexBufferBase, dataSize);

        // 执行绘制
        Flush();
    }

    //==========================================================================
    // 刷新：绑定所有纹理并执行一次绘制调用
    //==========================================================================
    void Renderer2D::Flush()
    {
        // 将本批次用到的所有纹理绑定到对应的纹理单元
        for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
        {
            s_Data.TextureSlots[i]->Bind(i);  // 纹理i绑定到纹理单元i
        }

        // 一次绘制调用渲染所有累积的四边形
        RenderCommand::DrawIndexed(s_Data.QuadVertexArray, s_Data.QuadIndexCount);
    }

    //==========================================================================
    // 绘制纯色四边形（2D坐标版本）
    //==========================================================================
    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color)
    {
        RF_PROFILE_FUNCTION();
        DrawQuad({ position.x, position.y, 0.0f }, size, color);
    }

    //==========================================================================
    // 绘制纯色四边形（3D坐标版本）
    //==========================================================================
    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
    {
        RF_PROFILE_FUNCTION();

        const float textureIndex = 0.0f;  // 使用白色纹理（索引0）

        // 计算变换矩阵：平移 + 缩放
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        //----- 写入4个顶点的数据到缓冲区 -----
        // 顶点1：左下
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[0];
        s_Data.QuadVertexBufferPtr->TexCoord = { 0.0f, 0.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        // 顶点2：右下
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[1];
        s_Data.QuadVertexBufferPtr->TexCoord = { 1.0f, 0.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        // 顶点3：右上
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[2];
        s_Data.QuadVertexBufferPtr->TexCoord = { 1.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        // 顶点4：左上
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[3];
        s_Data.QuadVertexBufferPtr->TexCoord = { 0.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        // 更新索引计数（每个四边形6个索引）
        s_Data.QuadIndexCount += 6;
    }

    //==========================================================================
    // 绘制纹理四边形（2D坐标版本）
    //==========================================================================
    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size,
        const Ref<Texture2D>& texture, float tilingFactor,
        const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();
        DrawQuad({ position.x, position.y, 0.0f }, size, texture, tilingFactor, tintColor);
    }

    //==========================================================================
    // 绘制纹理四边形（3D坐标版本）- 核心实现
    //==========================================================================
    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size,
        const Ref<Texture2D>& texture, float tilingFactor,
        const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();

        // 顶点颜色设为白色，最终颜色 = 白色 × 纹理颜色 × tintColor
        constexpr glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

        // 计算变换矩阵：平移 + 缩放
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        //----- 纹理索引管理 -----
        float textureIndex = 0.0f;  // 临时变量：记录当前四边形使用的纹理槽

        // 步骤1：检查纹理是否已在本批次中使用过
        for (uint32_t i = 1; i < s_Data.TextureSlotIndex; i++)
        {
            // 通过纹理ID比较（operator==重载）
            if (*s_Data.TextureSlots[i].get() == *texture.get())
            {
                textureIndex = (float)i;  // 复用已有纹理槽
                break;
            }
        }

        // 步骤2：如果是新纹理，分配新纹理槽
        if (textureIndex == 0.0f)
        {
            textureIndex = (float)s_Data.TextureSlotIndex;           // 新索引 = 当前可用槽
            s_Data.TextureSlots[s_Data.TextureSlotIndex] = texture;  // 存入纹理数组
            s_Data.TextureSlotIndex++;                                // 更新下一个可用槽
        }

        //----- 写入4个顶点的数据到缓冲区 -----
        // 所有顶点使用相同的纹理索引
        // 顶点1：左下
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[0];
        s_Data.QuadVertexBufferPtr->TexCoord = { 0.0f, 0.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        // 顶点2：右下
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[1];
        s_Data.QuadVertexBufferPtr->TexCoord = { 1.0f, 0.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        // 顶点3：右上
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[2];
        s_Data.QuadVertexBufferPtr->TexCoord = { 1.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        // 顶点4：左上
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[3];
        s_Data.QuadVertexBufferPtr->TexCoord = { 0.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        // 更新索引计数
        s_Data.QuadIndexCount += 6;
    }

    //==========================================================================
    // 绘制旋转纯色四边形（2D坐标版本）
    //==========================================================================
    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size,
        float rotation, const glm::vec4& color)
    {
        RF_PROFILE_FUNCTION();
        DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, color);
    }

    //==========================================================================
    // 绘制旋转纯色四边形（3D坐标版本）- 批处理版本
    //==========================================================================
    void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size,
        float rotation, const glm::vec4& color)
    {
        RF_PROFILE_FUNCTION();

        // 计算变换矩阵：平移 + 旋转
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
            * glm::rotate(glm::mat4(1.0f), glm::radians(rotation), { 0.0f, 0.0f, 1.0f });

        //----- 写入4个顶点的数据到缓冲区 -----
        // 使用白色纹理（索引0）
        // 顶点1：左下
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[0];
        s_Data.QuadVertexBufferPtr->TexCoord = { 0.0f, 0.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = 0.0f;
        s_Data.QuadVertexBufferPtr++;

        // 顶点2：右下
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[1];
        s_Data.QuadVertexBufferPtr->TexCoord = { 1.0f, 0.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = 0.0f;
        s_Data.QuadVertexBufferPtr++;

        // 顶点3：右上
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[2];
        s_Data.QuadVertexBufferPtr->TexCoord = { 1.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = 0.0f;
        s_Data.QuadVertexBufferPtr++;

        // 顶点4：左上
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[3];
        s_Data.QuadVertexBufferPtr->TexCoord = { 0.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = 0.0f;
        s_Data.QuadVertexBufferPtr++;

        // 更新索引计数
        s_Data.QuadIndexCount += 6;
    }

    //==========================================================================
    // 绘制旋转纹理四边形（2D坐标版本）
    //==========================================================================
    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size,
        float rotation, const Ref<Texture2D>& texture,
        float tilingFactor, const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();
        DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, texture, tilingFactor, tintColor);
    }

    //==========================================================================
    // 绘制旋转纹理四边形（3D坐标版本）- 批处理版本
    //==========================================================================
    void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size,
        float rotation, const Ref<Texture2D>& texture,
        float tilingFactor, const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();

        // 顶点颜色设为白色，最终颜色 = 白色 × 纹理颜色 × tintColor
        constexpr glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

        //----- 纹理索引管理 -----
        float textureIndex = 0.0f;  // 临时变量：记录当前四边形使用的纹理槽

        // 步骤1：检查纹理是否已在本批次中使用过
        for (uint32_t i = 1; i < s_Data.TextureSlotIndex; i++)
        {
            if (*s_Data.TextureSlots[i].get() == *texture.get())
            {
                textureIndex = (float)i;  // 复用已有纹理槽
                break;
            }
        }

        // 步骤2：如果是新纹理，分配新纹理槽
        if (textureIndex == 0.0f)
        {
            textureIndex = (float)s_Data.TextureSlotIndex;           // 新索引 = 当前可用槽
            s_Data.TextureSlots[s_Data.TextureSlotIndex] = texture;  // 存入纹理数组
            s_Data.TextureSlotIndex++;                                // 更新下一个可用槽
        }

        // 计算变换矩阵：平移 + 旋转 + 缩放
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
            * glm::rotate(glm::mat4(1.0f), glm::radians(rotation), { 0.0f, 0.0f, 1.0f })
            * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        //----- 写入4个顶点的数据到缓冲区 -----
        // 所有顶点使用相同的纹理索引
        // 顶点1：左下
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[0];
        s_Data.QuadVertexBufferPtr->TexCoord = { 0.0f, 0.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        // 顶点2：右下
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[1];
        s_Data.QuadVertexBufferPtr->TexCoord = { 1.0f, 0.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        // 顶点3：右上
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[2];
        s_Data.QuadVertexBufferPtr->TexCoord = { 1.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        // 顶点4：左上
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[3];
        s_Data.QuadVertexBufferPtr->TexCoord = { 0.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        // 更新索引计数
        s_Data.QuadIndexCount += 6;
    }
}