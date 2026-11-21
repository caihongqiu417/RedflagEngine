#include"rfpch.h"
#include "OpenGLVertexArray.h"

#include"glad/glad.h"

namespace Redflag {
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
			RF_CORE_ASSERT(false, "Unknown ShaderDataType!");
			return 0;
		}
	}

	OpenGLVertexArray::OpenGLVertexArray()
	{
		RF_PROFILE_FUNCTION();

		glCreateVertexArrays(1, &m_RendererID);
	}

	OpenGLVertexArray::~OpenGLVertexArray()
	{
		glDeleteVertexArrays(1, &m_RendererID);
	}

	void OpenGLVertexArray::Bind() const
	{
		glBindVertexArray(m_RendererID);
	}

	void OpenGLVertexArray::Unbind() const
	{
		glBindVertexArray(0);
	}

	// ---- 顶点与索引 ----
	void OpenGLVertexArray::AddVertexBuffer(const Redflag::Ref<VertexBuffer>& vertexBuffer)
	{
		RF_CORE_ASSERT(vertexBuffer->GetLayout().GetElements().size(), "VBO has no layout!");
		
		// 先选网格的顶点数组和顶点缓冲，OpenGL 才能记下这个网格的位置、颜色等属性从哪里读。
		glBindVertexArray(m_RendererID);
		vertexBuffer->Bind();
		

		uint32_t index = 0;
		const auto& layout = vertexBuffer->GetLayout();

		for (const auto& element : layout)
		{
			glEnableVertexAttribArray(index);

			// 实体编号在着色器里是 int，要用读取整数的接口。
			if (element.Type == ShaderDataType::Int ||
				element.Type == ShaderDataType::Int2 ||
				element.Type == ShaderDataType::Int3 ||
				element.Type == ShaderDataType::Int4)
			{
				glVertexAttribIPointer(
					index,
					element.GetComponentCount(),
					ShaderDataTypeToOpenGLBaseType(element.Type),
					layout.GetStride() /* 相邻两个顶点相隔的字节数 */,
					(const void*)(uintptr_t)element.Offset /* 这个属性距顶点开头的字节数 */
				);
			}
			else
			{
				glVertexAttribPointer(
					index,
					element.GetComponentCount(),
					ShaderDataTypeToOpenGLBaseType(element.Type),
					element.Normalized ? GL_TRUE : GL_FALSE,
					layout.GetStride(),
					(const void*)(uintptr_t)element.Offset
				);
			}

			index++;
		}
		m_VertexBuffers.push_back(vertexBuffer);
	}


	// 索引跟着顶点数组一起切换。先选这个网格，再指定它要用的索引。
	void OpenGLVertexArray::SetIndexBuffer(const Redflag::Ref<IndexBuffer>& indexBuffer)
	{
		glBindVertexArray(m_RendererID);
		indexBuffer->Bind();

		m_IndexBuffer = indexBuffer;
	}

}
