#include "rfpch.h"
#include "Renderer2D.h"

#include "VertexArray.h"
#include "Shader.h"
#include "RenderCommand.h"
#include"UniformBuffer.h"

#include <glm/gtc/matrix_transform.hpp>

//==============================================================================
// 2D��������Ⱦ��ʵ��
// ���ܣ�֧�����������ı��Σ��������򲻴����������Ż���Ⱦ����
// ���Ļ��ƣ�CPU���ۻ��������ݣ�һ���ϴ�GPU��һ�λ��Ƶ�����Ⱦ����ı���
//==============================================================================

namespace Redflag
{
    //--------------------------------------------------------------------------
    // ����ṹ�壺���������������������
    //--------------------------------------------------------------------------
    struct QuadVertex
    {
        glm::vec3 Position;
        glm::vec4 Color;
        glm::vec2 TexCoord;
        float textureIndex;
        float TilingFactor;
        int EntityID=0;
    };

    struct CircleVertex
    {
        glm::vec3 WorldPosition;
        glm::vec3 LocalPosition;
        float Thickness;
        glm::vec4 Color;
        float Fade;

        int EntityID = 0;
    };

    struct LineVertex
    {
        glm::vec3 Position;
        glm::vec4 Color;
    };

    //--------------------------------------------------------------------------
    // ��Ⱦ���ڲ����ݣ��洢������Ⱦ״̬����Դ������ģʽ��
    //--------------------------------------------------------------------------
    struct Renderer2DData
    {
        //========== �������� ==========
        static const uint32_t MaxQuads =10000;                    // ����������ı�������
        static const uint32_t MaxVertices = MaxQuads * 4;          // ��󶥵�����4����/�ı��Σ�
        static const uint32_t MaxIndices = MaxQuads * 6;           // �����������6����/�ı��Σ�
        static const uint32_t MaxTextureSlots = 32;         // ���������������Ӳ�����ƣ�

        //========== GPU��Դ ==========
        Ref<VertexArray> QuadVertexArray;                    // �����������VAO��
        Ref<VertexBuffer> QuadVertexBuffer;                  // ���㻺������VBO��
        Ref<Shader> QuadShader;                            // ������ɫ��
        Ref<Texture2D> WhiteTexture;                      

        Ref<VertexArray> CircleVertexArray;
        Ref<VertexBuffer> CircleVertexBuffer;
        Ref<Shader> CircleShader;

        //========== ������״̬ ==========
        uint32_t QuadIndexCount = 0;                          // ��ǰ������ʹ�õ���������
        QuadVertex* QuadVertexBufferBase = nullptr;           // CPU�˶��㻺������ַ
        QuadVertex* QuadVertexBufferPtr = nullptr;            // CPU�˶��㻺������ǰд��ָ��

        uint32_t CircleIndexCount = 0;
        CircleVertex* CircleVertexBufferBase = nullptr;
        CircleVertex* CircleVertexBufferPtr = nullptr;

        //========== �������� ==========
        std::array<Ref<Texture2D>, MaxTextureSlots> TextureSlots;   // ��ǰ����ʹ�õ���������
        uint32_t TextureSlotIndex = 1;                               // ��һ������������������0Ԥ������ɫ������

        //========== �������� ==========
        glm::vec4 QuadVertexPositions[4];                    // �ı��α�׼����λ�ã�����Ϊԭ�㣬��Χ-0.5~0.5��

        Ref<UniformBuffer> CameraUniformBuffer;

        // ── 线段 ──
        static const uint32_t MaxLines    = 4096;
        static const uint32_t MaxLineVerts = MaxLines * 2;
        Ref<VertexArray>  LineVertexArray;
        Ref<VertexBuffer> LineVertexBuffer;
        Ref<Shader>       LineShader;
        uint32_t          LineVertexCount = 0;
        LineVertex*       LineVertexBufferBase = nullptr;
        LineVertex*       LineVertexBufferPtr  = nullptr;

        Renderer2D::Statistics Stas;
    };

    

    // ȫ�־�̬��Ⱦ����ʵ��
    static Renderer2DData s_Data;

    //==========================================================================
    // ��ʼ������������ʱ����һ�Σ���������GPU��Դ�ͳ�ʼ״̬
    //==========================================================================
    void Renderer2D::Init()
    {
        RF_PROFILE_FUNCTION();

        //---------- 1. ��������������� ----------
        s_Data.QuadVertexArray = Redflag::VertexArray::Create();

        //---------- 2. �������㻺���� ----------
        // Ԥ����󶥵������ڴ�ռ�
        s_Data.QuadVertexBuffer = VertexBuffer::Create(s_Data.MaxVertices * sizeof(QuadVertex));

        //---------- 3. ���ö��㲼�� ----------
        // ����GPUÿ�����Եĺ��塢���ͺ�λ��
        Redflag::BufferLayout squareLayout = {
            {Redflag::ShaderDataType::Float3, "a_Position"},  // λ�ã�3��float
            {Redflag::ShaderDataType::Float4, "a_Color"},     // ��ɫ��4��float
            {Redflag::ShaderDataType::Float2, "a_TexCoord"},  // �������꣺2��float
            {Redflag::ShaderDataType::Float, "a_TexIndex"},
            {Redflag::ShaderDataType::Float, "a_TilingFactor"} ,
            {Redflag::ShaderDataType::Int, "a_EntityID"} ,
        };
        s_Data.QuadVertexBuffer->SetLayout(squareLayout);
        s_Data.QuadVertexArray->AddVertexBuffer(s_Data.QuadVertexBuffer);

        //---------- 4. ����CPU�˶��㻺�����ڴ� ----------
        // ������ʱ�洢ÿ֡�Ķ�������
        s_Data.QuadVertexBufferBase = new QuadVertex[s_Data.MaxVertices];

        //---------- 5. �������������� ----------
        uint32_t* quadIndices = new uint32_t[s_Data.MaxIndices];
        for (uint32_t i = 0; i < s_Data.MaxQuads; i++)
        {
            // ÿ���ı�����2����������ɣ�����˳��0-1-2-2-3-0
            quadIndices[i * 6 + 0] = i * 4 + 0;  // ������1: ����0
            quadIndices[i * 6 + 1] = i * 4 + 1;  // ������1: ����1
            quadIndices[i * 6 + 2] = i * 4 + 2;  // ������1: ����2
            quadIndices[i * 6 + 3] = i * 4 + 2;  // ������2: ����2
            quadIndices[i * 6 + 4] = i * 4 + 3;  // ������2: ����3
            quadIndices[i * 6 + 5] = i * 4 + 0;  // ������2: ����0
        }
        Ref<IndexBuffer> quadIB = IndexBuffer::Create(quadIndices, s_Data.MaxIndices);
        s_Data.QuadVertexArray->SetIndexBuffer(quadIB);
        delete[] quadIndices;

        //Circles
        s_Data.CircleVertexArray = VertexArray::Create();
        s_Data.CircleVertexBuffer = VertexBuffer::Create(s_Data.MaxVertices * sizeof(CircleVertex));
        s_Data.CircleVertexBuffer->SetLayout({
        { ShaderDataType::Float3, "a_WorldPosition"  },
        { ShaderDataType::Float3, "a_LocalPosition"  },
        { ShaderDataType::Float,  "a_Thickness" },
        { ShaderDataType::Float4, "a_Color"     },
        { ShaderDataType::Float,  "a_Fade"      },
        { ShaderDataType::Int,    "a_EntityID"  }
         });
        s_Data.CircleVertexArray->AddVertexBuffer(s_Data.CircleVertexBuffer);
        s_Data.CircleVertexArray->SetIndexBuffer(quadIB); //use quad IB
        s_Data.CircleVertexBufferBase = new CircleVertex[s_Data.MaxVertices];

        //---------- 6. ������ɫ���� ----------
        // ���ڻ��ƴ�ɫ�ı��Σ�ͨ��������ɫ��ɫ��
        s_Data.WhiteTexture = Redflag::Texture2D::Create(1, 1);
        uint32_t whiteTextureData = 0xffffffff;  // RGBA��ɫ����͸����
        s_Data.WhiteTexture->SetData(&whiteTextureData, sizeof(whiteTextureData));

        //---------- 7. ׼���������������� ----------
        // ������ɫ��ÿ����������Ӧ��������Ԫ���
        int32_t samplers[s_Data.MaxTextureSlots];
        for (uint32_t i = 0; i < s_Data.MaxTextureSlots; i++)
            samplers[i] = i;  // ������i��Ӧ������Ԫi

        //---------- 8. ����������������ɫ�� ----------
        s_Data.QuadShader = Redflag::Shader::Create("assets/shaders/Renderer2D_Quad.glsl");
        s_Data.CircleShader = Redflag::Shader::Create("assets/shaders/Renderer2D_Circle.glsl");

        s_Data.QuadShader->Bind();
        s_Data.QuadShader->SetIntArray("u_Textures", samplers, s_Data.MaxTextureSlots);

        //---------- 9. ��ʼ�������� ----------
        // 0�Ų����ð󶨰�ɫ����
        s_Data.TextureSlots[0] = s_Data.WhiteTexture;

        //---------- 10. ��ʼ����׼�ı��ζ���λ�� ----------
        // ������Ϊԭ�㣬��Χ-0.5~0.5�����ڱ任
        s_Data.QuadVertexPositions[0] = { -0.5f, -0.5f, 0.0f, 1.0f };  // ����
        s_Data.QuadVertexPositions[1] = { 0.5f, -0.5f, 0.0f, 1.0f };   // ����
        s_Data.QuadVertexPositions[2] = { 0.5f, 0.5f, 0.0f, 1.0f };    // ����
        s_Data.QuadVertexPositions[3] = { -0.5f, 0.5f, 0.0f, 1.0f };   // ����

        // [����] ���� Camera Uniform Buffer
        // ��С�� sizeof(mat4) = 64�ֽ�
        // Binding Point = 0 (����Լ�� 0 �Ų�λ�� Camera)
        s_Data.CameraUniformBuffer = UniformBuffer::Create(sizeof(glm::mat4), 0);

        // ── 初始化线段缓冲 ──
        s_Data.LineVertexArray  = VertexArray::Create();
        s_Data.LineVertexBuffer = VertexBuffer::Create(s_Data.MaxLineVerts * sizeof(LineVertex));
        s_Data.LineVertexBuffer->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float4, "a_Color"    },
        });
        s_Data.LineVertexArray->AddVertexBuffer(s_Data.LineVertexBuffer);
        s_Data.LineVertexBufferBase = new LineVertex[s_Data.MaxLineVerts];
        s_Data.LineShader = Shader::Create("assets/shaders/Line.glsl");
    }

    //==========================================================================
    // �رգ��������ʱ���ã��ͷ���Դ
    //==========================================================================
    void Renderer2D::Shutdown()
    {
        RF_PROFILE_FUNCTION();
        delete[] s_Data.QuadVertexBufferBase;
        delete[] s_Data.LineVertexBufferBase;
        delete[] s_Data.CircleVertexBufferBase;
    }

    //==========================================================================
    // ��ʼ�����Σ��༭�����
    //==========================================================================
    void Renderer2D::BeginScene(const EditorCamera& camera)
    {
        RF_PROFILE_FUNCTION();

        glm::mat4 viewProj =camera.GetViewProjection();

        // [�޸ĺ�]
        // ֻ��Ҫ���� UBO������Ҫ Bind Shader (��Ϊ UBO ��ȫ�ְ󶨵�)
        s_Data.CameraUniformBuffer->SetData(&viewProj, sizeof(glm::mat4));


        // ────────────────────────────────────────────────────────────
        s_Data.QuadIndexCount = 0;
        s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;
        s_Data.TextureSlotIndex = 1;

        s_Data.CircleIndexCount = 0;
        s_Data.CircleVertexBufferPtr = s_Data.CircleVertexBufferBase;

        s_Data.LineVertexCount = 0;
        s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;
    }

    //==========================================================================
    // ��ʼ�����Σ�ÿ֡��ʼʱ���ã�����������״̬
    //==========================================================================
    void Renderer2D::BeginScene(const glm::mat4 projection, const glm::mat4& transform)
    {
        RF_PROFILE_FUNCTION();

        glm::mat4 viewProj = projection * glm::inverse(transform);

        // [�޸ĺ�]
         // ֻ��Ҫ���� UBO������Ҫ Bind Shader (��Ϊ UBO ��ȫ�ְ󶨵�)
        s_Data.CameraUniformBuffer->SetData(&viewProj, sizeof(glm::mat4));


        s_Data.QuadIndexCount = 0;
        s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;
        s_Data.TextureSlotIndex = 1;

        s_Data.CircleIndexCount = 0;
        s_Data.CircleVertexBufferPtr = s_Data.CircleVertexBufferBase;

        s_Data.LineVertexCount = 0;
        s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;
    }

    //==========================================================================
    // ��ʼ�����Σ�ÿ֡��ʼʱ���ã�����������״̬(To Remove)
    //==========================================================================
    void Renderer2D::BeginScene(const OrthographicCamera& camera)
    {
        RF_PROFILE_FUNCTION();

        // ����ɫ����������ͼͶӰ����
        s_Data.QuadShader->Bind();
        s_Data.QuadShader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());

        // ����������״̬
        s_Data.QuadIndexCount = 0;                           // �����������
        s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;  // ����д��ָ�뵽��������ͷ
        s_Data.TextureSlotIndex = 1;                          // ����������������0���ã�
    }

    //==========================================================================
    // ������ǰ���Σ��ϴ��������ݵ�GPU��ִ�л���
    //==========================================================================
    void Renderer2D::EndScene()
    {
        RF_PROFILE_FUNCTION();

        if (s_Data.QuadIndexCount > 0)
        {
            uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.QuadVertexBufferPtr -
                (uint8_t*)s_Data.QuadVertexBufferBase);
            s_Data.QuadVertexBuffer->SetData(s_Data.QuadVertexBufferBase, dataSize);
            Flush();
        }

        if (s_Data.CircleIndexCount > 0)
        {
            uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.CircleVertexBufferPtr -
                (uint8_t*)s_Data.CircleVertexBufferBase);
            s_Data.CircleVertexBuffer->SetData(s_Data.CircleVertexBufferBase, dataSize);

            s_Data.CircleShader->Bind();
            s_Data.CircleVertexArray->Bind();
            RenderCommand::DrawIndexed(s_Data.CircleVertexArray, s_Data.CircleIndexCount);
            s_Data.Stas.DrawCalls++;
        }


        // 刷新线段
        if (s_Data.LineVertexCount > 0)
        {
            uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.LineVertexBufferPtr -
                (uint8_t*)s_Data.LineVertexBufferBase);
            s_Data.LineVertexBuffer->SetData(s_Data.LineVertexBufferBase, dataSize);

            s_Data.LineShader->Bind();
            s_Data.LineVertexArray->Bind();
            RenderCommand::DrawLines(s_Data.LineVertexArray, s_Data.LineVertexCount);
            s_Data.Stas.DrawCalls++;
        }
    }

    //==========================================================================
    // ˢ�£�������������ִ��һ�λ��Ƶ���
    //==========================================================================
    void Renderer2D::Flush()
    {
        s_Data.QuadShader->Bind();
        s_Data.QuadVertexArray->Bind();

        // ���������õ������������󶨵���Ӧ��������Ԫ
        for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
        {
            s_Data.TextureSlots[i]->Bind(i);  // ����i�󶨵�������Ԫi
        }

        // һ�λ��Ƶ�����Ⱦ�����ۻ����ı���
        RenderCommand::DrawIndexed(s_Data.QuadVertexArray, s_Data.QuadIndexCount);

		s_Data.Stas.DrawCalls++;  // ����ͳ�����ݣ����Ƶ��ô�������
    }

    void Renderer2D::FlushAndReset()
    {
		EndScene();  // ������ǰ���Σ��ϴ����ݲ����ƣ�

        s_Data.QuadIndexCount = 0;                           // �����������
        s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;  // ����д��ָ�뵽��������ͷ
        s_Data.TextureSlotIndex = 1;                          // ����������������0���ã�
	}





    //==========================================================================
    // ���ƴ�ɫ�ı��Σ�2D����汾��
    //==========================================================================
    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color)
    {
        RF_PROFILE_FUNCTION();
        DrawQuad({ position.x, position.y, 0.0f }, size, color);
    }

    //==========================================================================
    // ���ƴ�ɫ�ı��Σ�3D����汾��
    //==========================================================================
    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
    {
        RF_PROFILE_FUNCTION();

        // ����任����ƽ�� + ����
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        DrawQuad(transform, color);
    }

    //==========================================================================
    // ���������ı��Σ�2D����汾��
    //==========================================================================
    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size,
        const Ref<Texture2D>& texture, float tilingFactor,
        const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();
        DrawQuad({ position.x, position.y, 0.0f }, size, texture, tilingFactor, tintColor);
    }

    //==========================================================================
    // ���������ı��Σ�3D����汾��
    //==========================================================================
    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size,
        const Ref<Texture2D>& texture, float tilingFactor,
        const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        DrawQuad(transform, texture, tilingFactor, tintColor);
    }

    //==========================================================================
    // �����������ı��Σ�2D����汾��
    //==========================================================================
    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size,
        const Ref<SubTexture2D>& subTexture, float tilingFactor, const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();
        DrawQuad({ position.x, position.y, 0.0f }, size, subTexture, tilingFactor, tintColor);
    }

    //==========================================================================
    // �����������ı��Σ�3D����汾��
    //==========================================================================
    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size,
        const Ref<SubTexture2D>& subTexture, float tilingFactor, const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();

        if (s_Data.QuadIndexCount >= s_Data.MaxIndices)
            FlushAndReset();  // �����ǰ������������ʼ������

        // ������ɫ��Ϊ��ɫ��������ɫ = ��ɫ �� ������ɫ �� tintColor
        constexpr glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

		// ��SubTexture2D��ȡ���������������Դ
		const glm::vec2* texCoords = subTexture->GetTexCoords(); 
		const Ref<Texture2D>& texture = subTexture->GetTexture();  

        // ����任����ƽ�� + ����
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        //----- ������������ -----
        float textureIndex = 0.0f;  // ��ʱ��������¼��ǰ�ı���ʹ�õ�������

        // ����1����������Ƿ����ڱ�������ʹ�ù�
        for (uint32_t i = 1; i < s_Data.TextureSlotIndex; i++)
        {
            // ͨ������ID�Ƚϣ�operator==���أ�
            if (*s_Data.TextureSlots[i].get() == *texture.get())
            {
                textureIndex = (float)i;  // ��������������
                break;
            }
        }

        // ����2���������������������������
        if (textureIndex == 0.0f)
        {
            textureIndex = (float)s_Data.TextureSlotIndex;           // ������ = ��ǰ���ò�
            s_Data.TextureSlots[s_Data.TextureSlotIndex] = texture;  // ������������
            s_Data.TextureSlotIndex++;                                // ������һ�����ò�
        }

        //----- д��4����������ݵ������� -----
        for (uint32_t i = 0; i < 4; i++)
        {
            s_Data.QuadVertexBufferPtr->Color = color;
            s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[i];
            s_Data.QuadVertexBufferPtr->TexCoord = texCoords[i];
            s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
            s_Data.QuadVertexBufferPtr++;
        }

        // ������������
        s_Data.QuadIndexCount += 6;

        s_Data.Stas.QuadCount++;  // ����ͳ�����ݣ��ı�����������
	}
    //==========================================================================
// ���ƴ�ɫ�ı��Σ�ECS��- ʹ�ñ任��������Transform�����
//==========================================================================
    void Renderer2D::DrawQuad(const glm::mat4& transform,
        const glm::vec4& color,
        int entityID)
    {
        RF_PROFILE_FUNCTION();

        if (s_Data.QuadIndexCount >= s_Data.MaxIndices)
            FlushAndReset();

        const glm::vec2 texCoords[4] = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f}
        };

        for (uint32_t i = 0; i < 4; i++)
        {
            s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[i];
            s_Data.QuadVertexBufferPtr->Color = color;
            s_Data.QuadVertexBufferPtr->TexCoord = texCoords[i];
            s_Data.QuadVertexBufferPtr->textureIndex = 0.0f;
            s_Data.QuadVertexBufferPtr->TilingFactor = 1.0f;  
            s_Data.QuadVertexBufferPtr->EntityID = entityID;   
            s_Data.QuadVertexBufferPtr++;
        }

        s_Data.QuadIndexCount += 6;
        s_Data.Stas.QuadCount++;
    }

    //==========================================================================
    // ���������ı��Σ�ECS��- ʹ�ñ任��������Transform�����
    //==========================================================================
    void Renderer2D::DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture,
        float tilingFactor, const glm::vec4& tintColor,int entityID)
    {
        RF_PROFILE_FUNCTION();

        // ��������Ƿ����������������ˢ�²���ʼ������
        if (s_Data.QuadIndexCount >= s_Data.MaxIndices)
            FlushAndReset();

        // ������ɫ��Ϊ��ɫ��������ɫ = ��ɫ �� ������ɫ �� tintColor
        constexpr glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

        //----- ������������ -----
        float textureIndex = 0.0f;

        // ��������Ƿ����ڱ�������ʹ�ù�
        for (uint32_t i = 1; i < s_Data.TextureSlotIndex; i++)
        {
            if (*s_Data.TextureSlots[i].get() == *texture.get())
            {
                textureIndex = (float)i;  // ��������������
                break;
            }
        }

        // �������������������������
        if (textureIndex == 0.0f)
        {
            // ����������Ƿ�����
            if (s_Data.TextureSlotIndex >= s_Data.MaxTextureSlots)
                FlushAndReset();  // ������������ˢ������

            textureIndex = (float)s_Data.TextureSlotIndex;
            s_Data.TextureSlots[s_Data.TextureSlotIndex] = texture;
            s_Data.TextureSlotIndex++;
        }

        // ��׼��������
        const glm::vec2 texCoords[4] = {
            {0.0f, 0.0f},  // ����
            {1.0f, 0.0f},  // ����
            {1.0f, 1.0f},  // ����
            {0.0f, 1.0f}   // ����
        };

        // д��4����������ݵ�������
        for (uint32_t i = 0; i < 4; i++)
        {
            s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[i];
            s_Data.QuadVertexBufferPtr->Color = color;
            s_Data.QuadVertexBufferPtr->TexCoord = texCoords[i]; 
            s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
            s_Data.QuadVertexBufferPtr->TilingFactor = tilingFactor;
            s_Data.QuadVertexBufferPtr->EntityID = entityID;
            s_Data.QuadVertexBufferPtr++;
        }

        // ������������
        s_Data.QuadIndexCount += 6;

        // ����ͳ������
        s_Data.Stas.QuadCount++;
    }

    //==========================================================================
    // 绘制子纹理四边形（ECS版 - 使用变换矩阵 + SubTexture2D）
    //==========================================================================
    void Renderer2D::DrawQuad(const glm::mat4& transform, const Ref<SubTexture2D>& subTexture,
        float tilingFactor, const glm::vec4& tintColor, int entityID)
    {
        RF_PROFILE_FUNCTION();

        if (s_Data.QuadIndexCount >= s_Data.MaxIndices)
            FlushAndReset();

        constexpr glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

        const glm::vec2* texCoords = subTexture->GetTexCoords();
        const Ref<Texture2D>& texture = subTexture->GetTexture();

        float textureIndex = 0.0f;

        for (uint32_t i = 1; i < s_Data.TextureSlotIndex; i++)
        {
            if (*s_Data.TextureSlots[i].get() == *texture.get())
            {
                textureIndex = (float)i;
                break;
            }
        }

        if (textureIndex == 0.0f)
        {
            if (s_Data.TextureSlotIndex >= s_Data.MaxTextureSlots)
                FlushAndReset();

            textureIndex = (float)s_Data.TextureSlotIndex;
            s_Data.TextureSlots[s_Data.TextureSlotIndex] = texture;
            s_Data.TextureSlotIndex++;
        }

        for (uint32_t i = 0; i < 4; i++)
        {
            s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[i];
            s_Data.QuadVertexBufferPtr->Color = tintColor;
            s_Data.QuadVertexBufferPtr->TexCoord = texCoords[i];
            s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
            s_Data.QuadVertexBufferPtr->TilingFactor = tilingFactor;
            s_Data.QuadVertexBufferPtr->EntityID = entityID;
            s_Data.QuadVertexBufferPtr++;
        }

        s_Data.QuadIndexCount += 6;
        s_Data.Stas.QuadCount++;
    }

    //==========================================================================
    // ������ת��ɫ�ı��Σ�2D����汾��
    //==========================================================================
    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size,
        float rotation, const glm::vec4& color)
    {
        RF_PROFILE_FUNCTION();
        DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, color);
    }

    //==========================================================================
    // ������ת��ɫ�ı��Σ�3D����汾��
    //==========================================================================
    void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size,
        float rotation, const glm::vec4& color)
    {
        RF_PROFILE_FUNCTION();

        if (s_Data.QuadIndexCount >= s_Data.MaxIndices)
            FlushAndReset();  // �����ǰ������������ʼ������

        // ����任����ƽ�� + ��ת
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
            * glm::rotate(glm::mat4(1.0f), rotation, { 0.0f, 0.0f, 1.0f })
            * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        //----- д��4����������ݵ������� -----
        const glm::vec2 texCoords[4] = {
            {0.0f, 0.0f},  // ����
            {1.0f, 0.0f},  // ����
            {1.0f, 1.0f},  // ����
            {0.0f, 1.0f}   // ����
        };

        // ʹ�� for ѭ��д��4������
        for (uint32_t i = 0; i < 4; i++)
        {
            s_Data.QuadVertexBufferPtr->Color = color;
            s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[i];
            s_Data.QuadVertexBufferPtr->TexCoord = texCoords[i];
            s_Data.QuadVertexBufferPtr->textureIndex = 0.0f;
            s_Data.QuadVertexBufferPtr++;
        }

        // ������������
        s_Data.QuadIndexCount += 6;

        s_Data.Stas.QuadCount++;  // ����ͳ�����ݣ��ı�����������
    }

    //==========================================================================
    // ������ת�����ı��Σ�2D����汾��
    //==========================================================================
    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size,
        float rotation, const Ref<Texture2D>& texture,
        float tilingFactor, const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();
        DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, texture, tilingFactor, tintColor);
    }

    //==========================================================================
    // ������ת�����ı��Σ�3D����汾��
    //==========================================================================
    void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size,
        float rotation, const Ref<Texture2D>& texture,
        float tilingFactor, const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();

        if (s_Data.QuadIndexCount >= s_Data.MaxIndices)
            FlushAndReset();  // �����ǰ������������ʼ������

        // ������ɫ��Ϊ��ɫ��������ɫ = ��ɫ �� ������ɫ �� tintColor
        constexpr glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

        //----- ������������ -----
        float textureIndex = 0.0f;  // ��ʱ��������¼��ǰ�ı���ʹ�õ�������

        // ����1����������Ƿ����ڱ�������ʹ�ù�
        for (uint32_t i = 1; i < s_Data.TextureSlotIndex; i++)
        {
            if (*s_Data.TextureSlots[i].get() == *texture.get())
            {
                textureIndex = (float)i;  // ��������������
                break;
            }
        }

        // ����2���������������������������
        if (textureIndex == 0.0f)
        {
            textureIndex = (float)s_Data.TextureSlotIndex;           // ������ = ��ǰ���ò�
            s_Data.TextureSlots[s_Data.TextureSlotIndex] = texture;  // ������������
            s_Data.TextureSlotIndex++;                                // ������һ�����ò�
        }

        // ����任����ƽ�� + ��ת + ����
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
            * glm::rotate(glm::mat4(1.0f), rotation, { 0.0f, 0.0f, 1.0f })
            * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        //----- д��4����������ݵ������� -----
        const glm::vec2 texCoords[4] = {
            {0.0f, 0.0f},  // ����
            {1.0f, 0.0f},  // ����
            {1.0f, 1.0f},  // ����
            {0.0f, 1.0f}   // ����
        };

        // ʹ�� for ѭ��д��4������
        for (uint32_t i = 0; i < 4; i++)
        {
            s_Data.QuadVertexBufferPtr->Color = color;
            s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[i];
            s_Data.QuadVertexBufferPtr->TexCoord = texCoords[i];
            s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
            s_Data.QuadVertexBufferPtr++;
        }

        // ������������
        s_Data.QuadIndexCount += 6;

		s_Data.Stas.QuadCount++;  // ����ͳ�����ݣ��ı�����������
    }

    //==========================================================================
    // ������ת�������ı��Σ�2D����汾��
    //==========================================================================
    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size,
        float rotation, const Ref<SubTexture2D>& subTexture,
        float tilingFactor, const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();
        DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, subTexture, tilingFactor, tintColor);
	}

	//==========================================================================
	// ������ת�������ı��Σ�3D����汾��
	//==========================================================================
    void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size,
        float rotation, const Ref<SubTexture2D>& subTexture,
        float tilingFactor, const glm::vec4& tintColor)
    {
        RF_PROFILE_FUNCTION();
        
        // ��SubTexture2D��ȡ���������������Դ
        const glm::vec2* texCoords = subTexture->GetTexCoords();
        const Ref<Texture2D>& texture = subTexture->GetTexture();

        if (s_Data.QuadIndexCount >= s_Data.MaxIndices)
            FlushAndReset();  // �����ǰ������������ʼ������

        // ������ɫ��Ϊ��ɫ��������ɫ = ��ɫ �� ������ɫ �� tintColor
        constexpr glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

        //----- ������������ -----
        float textureIndex = 0.0f;  // ��ʱ��������¼��ǰ�ı���ʹ�õ�������

        // ����1����������Ƿ����ڱ�������ʹ�ù�
        for (uint32_t i = 1; i < s_Data.TextureSlotIndex; i++)
        {
            if (*s_Data.TextureSlots[i].get() == *texture.get())
            {
                textureIndex = (float)i;  // ��������������
                break;
            }
        }

        // ����2���������������������������
        if (textureIndex == 0.0f)
        {
            textureIndex = (float)s_Data.TextureSlotIndex;           // ������ = ��ǰ���ò�
            s_Data.TextureSlots[s_Data.TextureSlotIndex] = texture;  // ������������
            s_Data.TextureSlotIndex++;                                // ������һ�����ò�
        }

        // ����任����ƽ�� + ��ת + ����
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
            * glm::rotate(glm::mat4(1.0f), rotation, { 0.0f, 0.0f, 1.0f })
            * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        //----- д��4����������ݵ������� -----
        for (uint32_t i = 0; i < 4; i++)
        {
            s_Data.QuadVertexBufferPtr->Color = color;
            s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPositions[i];
            s_Data.QuadVertexBufferPtr->TexCoord = texCoords[i];
            s_Data.QuadVertexBufferPtr->textureIndex = textureIndex;
            s_Data.QuadVertexBufferPtr++;
        }

        // ������������
        s_Data.QuadIndexCount += 6;

        s_Data.Stas.QuadCount++;  // ����ͳ�����ݣ��ı�����������
	}

    //Circle
    void Renderer2D::DrawCircle(const glm::mat4& transform, const glm::vec4& color,
        float thickness, float fade, int entityID)
    {
        RF_PROFILE_FUNCTION();


        if (s_Data.CircleIndexCount >= s_Data.MaxIndices)
        {
            // TODO: 实现 FlushAndReset 对圆形的支持
        }


        for (uint32_t i = 0; i < 4; i++)
        {
            s_Data.CircleVertexBufferPtr->WorldPosition = transform * s_Data.QuadVertexPositions[i];
            s_Data.CircleVertexBufferPtr->LocalPosition = s_Data.QuadVertexPositions[i]*2.f; //用于判断每个像素到圆心的距离
            s_Data.CircleVertexBufferPtr->Color = color;
            s_Data.CircleVertexBufferPtr->Thickness = thickness;
            s_Data.CircleVertexBufferPtr->Fade = fade;
            s_Data.CircleVertexBufferPtr->EntityID = entityID;
            s_Data.CircleVertexBufferPtr++;
        }

        s_Data.CircleIndexCount += 6;
        s_Data.Stas.QuadCount++;  
    }

    void Renderer2D::DrawSprite(const glm::mat4& transform, SpriteRendererComponent& src, int entityID)
    {
        if (src.Texture)
            DrawQuad(transform, src.Texture,src.TilingFactor ,src.Color, entityID);
        else
            DrawQuad(transform, src.Color, entityID);
    }

    void Renderer2D::ResetStats()
    {
		memset(&s_Data.Stas, 0, sizeof(Renderer2D::Statistics)); 
    }

    Renderer2D::Statistics Renderer2D::GetStats()
    {
        return s_Data.Stas;
	}

    void Renderer2D::DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color)
    {
        if (s_Data.LineVertexCount + 2 > s_Data.MaxLineVerts)
            return; // 超出容量直接丢弃（或可扩展为 flush）

        s_Data.LineVertexBufferPtr->Position = p0;
        s_Data.LineVertexBufferPtr->Color    = color;
        s_Data.LineVertexBufferPtr++;

        s_Data.LineVertexBufferPtr->Position = p1;
        s_Data.LineVertexBufferPtr->Color    = color;
        s_Data.LineVertexBufferPtr++;

        s_Data.LineVertexCount += 2;
    }

    void Renderer2D::SetLineWidth(float width)
    {
        RenderCommand::SetLineWidth(width);
    }
}