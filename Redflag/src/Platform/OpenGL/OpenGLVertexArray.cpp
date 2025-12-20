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





	void OpenGLVertexArray::AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer)
	{
		RF_CORE_ASSERT(vertexBuffer->GetLayout().GetElements().size(), "VBO has no layout!");
		
		// 绑定当前VAO（Vertex Array Object），所有的顶点属性设置都会记录到这个VAO中
		glBindVertexArray(m_RendererID);

		// 绑定VBO（Vertex Buffer Object），准备设置其顶点属性
		vertexBuffer->Bind();
		

		uint32_t index = 0;  // 顶点属性位置索引，从0开始递增
		const auto& layout = vertexBuffer->GetLayout();  // 获取顶点缓冲区的布局信息

		// 遍历布局中的每个顶点属性元素（如位置、法线、纹理坐标等）
		for (const auto& element : layout)
		{
			// 启用指定位置的顶点属性数组
			// 参数index：顶点属性的位置索引，对应着色器中的location
			glEnableVertexAttribArray(index);

			// 设置顶点属性指针，告诉OpenGL如何解析顶点数据
			glVertexAttribPointer(
				index,  // 顶点属性位置索引
				element.GetComponentCount(),  // 每个顶点的分量数量（如vec3为3）
				ShaderDataTypeToOpenGLBaseType(element.Type),  // 数据类型转换为OpenGL枚举
				element.Normalized ? GL_TRUE : GL_FALSE,  // 是否归一化
				layout.GetStride(),  // 顶点之间的字节跨度（步长）
				(const void*)element.Offset  // 该属性在顶点数据中的起始偏移量
			);

			index++;  // 递增索引，为下一个顶点属性做准备
		}

		// 将顶点缓冲区保存到成员变量中，防止被释放
		m_VertexBuffers.push_back(vertexBuffer);
	}


	// "设置"（通常只做一次!）
	void OpenGLVertexArray::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer)
	{
		glBindVertexArray(m_RendererID);
		indexBuffer->Bind();

		m_IndexBuffer = indexBuffer;
	}

}
