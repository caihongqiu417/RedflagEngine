#include "rfpch.h"
#include "OpenGLBuffer.h"
#include <glad/glad.h>

namespace Redflag {

	// =================================== VBO 实现 ===================================
	OpenGLVertexBuffer::OpenGLVertexBuffer(float* vertices, uint32_t size)
	{
		// 1. 创建缓冲区对象
		glCreateBuffers(1, &m_RendererID);

		// 2. 绑定到 GL_ARRAY_BUFFER（顶点数据缓冲区）,为了上传数据
		glBindBuffer(GL_ARRAY_BUFFER, m_RendererID); 
		// 3. 上传数据到GPU
		// GL_ARRAY_BUFFER: 顶点属性缓冲区
		// size: 数据总字节数
		// vertices: CPU端顶点数据
		// GL_STATIC_DRAW: 数据很少修改，适合多次绘制
		glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
	}

	OpenGLVertexBuffer::~OpenGLVertexBuffer()
	{
		glDeleteBuffers(1, &m_RendererID);
	}

	void OpenGLVertexBuffer::Bind() const
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
	}

	void OpenGLVertexBuffer::Unbind() const
	{
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	// =================================== IBO 实现 ===================================
	OpenGLIndexBuffer::OpenGLIndexBuffer(uint32_t* indices, uint32_t count)
		: m_Count(count)
	{
		//1.创建缓冲区对象
		glCreateBuffers(1, &m_RendererID);

		//2.绑定到GL_ELEMENT_ARRAY_BUFFER（GPU）
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);  

		//3.上传数据
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32_t), indices, GL_STATIC_DRAW);
	}

	OpenGLIndexBuffer::~OpenGLIndexBuffer()
	{
		glDeleteBuffers(1, &m_RendererID);
	}

	void OpenGLIndexBuffer::Bind() const
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
	}

	void OpenGLIndexBuffer::Unbind() const
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}
}