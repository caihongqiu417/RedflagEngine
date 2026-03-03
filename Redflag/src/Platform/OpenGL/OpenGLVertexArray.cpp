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
			// 错误处理：未知类型
			RF_CORE_ASSERT(false, "Unknown ShaderDataType!");
			return 0;
		}
	}

	OpenGLVertexArray::OpenGLVertexArray()
	{
		RF_PROFILE_FUNCTION();

		glCreateVertexArrays(1, &m_RendererID);
	}

	void OpenGLVertexArray::Bind() const
	{
		glBindVertexArray(m_RendererID);
	}

	void OpenGLVertexArray::Unbind() const
	{
		glBindVertexArray(0);
	}





	void OpenGLVertexArray::AddVertexBuffer(const Redflag::Ref<VertexBuffer>& vertexBuffer)
	{
		RF_CORE_ASSERT(vertexBuffer->GetLayout().GetElements().size(), "VBO has no layout!");
		
		// 绑定当前VAO（Vertex Array Object），所有的顶点属性设置都会记录到这个VAO中
		glBindVertexArray(m_RendererID);

		// 绑定VBO（Vertex Buffer Object），准备设置其顶点属性
		vertexBuffer->Bind();
		

		uint32_t index = 0;  // 顶点属性位置索引，从0开始递增
		const auto& layout = vertexBuffer->GetLayout();  // 获取顶点缓冲区的布局信息

		for (const auto& element : layout)
		{
			glEnableVertexAttribArray(index);

			if (element.Type == ShaderDataType::Int ||
				element.Type == ShaderDataType::Int2 ||
				element.Type == ShaderDataType::Int3 ||
				element.Type == ShaderDataType::Int4)
			{
				glVertexAttribIPointer(
					index,
					element.GetComponentCount(),
					ShaderDataTypeToOpenGLBaseType(element.Type),
					layout.GetStride(),
					(const void*)(uintptr_t)element.Offset
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
		// 将顶点缓冲区保存到成员变量中，防止被释放
		m_VertexBuffers.push_back(vertexBuffer);
	}


	// "设置"（通常只做一次!）
	void OpenGLVertexArray::SetIndexBuffer(const Redflag::Ref<IndexBuffer>& indexBuffer)
	{
		glBindVertexArray(m_RendererID);
		indexBuffer->Bind();

		m_IndexBuffer = indexBuffer;
	}

}
