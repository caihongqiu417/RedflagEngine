#include "rfpch.h"
#include "OpenGLBuffer.h"
#include <glad/glad.h>

namespace Redflag {
	OpenGLVertexBuffer::OpenGLVertexBuffer(uint32_t size)
	{
		RF_PROFILE_FUNCTION();
		glCreateBuffers(1, &m_RendererID);
		glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
		glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW); // 预留空间，数据稍后更新
	}

	// =================================== VBO 实现 ===================================
	OpenGLVertexBuffer::OpenGLVertexBuffer(float* vertices, uint32_t size)
	{
		RF_PROFILE_FUNCTION();

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
		RF_PROFILE_FUNCTION();

		glDeleteBuffers(1, &m_RendererID);
	}

	void OpenGLVertexBuffer::Bind() const
	{
		RF_PROFILE_FUNCTION();

		glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
	}

	void OpenGLVertexBuffer::Unbind() const
	{
		RF_PROFILE_FUNCTION();

		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	void OpenGLVertexBuffer::SetData(const void* data, uint32_t size)
	{
		RF_PROFILE_FUNCTION();

		glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
		glBufferSubData(GL_ARRAY_BUFFER, 0, size, data); //用subdata，因为之前已经用glBufferData分配了足够的空间，这里只是更新数据，不需要重新分配
	}

	// =================================== IBO 实现 ===================================
	OpenGLIndexBuffer::OpenGLIndexBuffer(uint32_t* indices, uint32_t count)
		: m_Count(count)
	{
		RF_PROFILE_FUNCTION();

		//1.创建缓冲区对象
		glCreateBuffers(1, &m_RendererID);

		//2.绑定到GL_ELEMENT_ARRAY_BUFFER（GPU）
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);  

		//3.上传数据
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32_t), indices, GL_STATIC_DRAW);
	}

	OpenGLIndexBuffer::~OpenGLIndexBuffer()
	{
		RF_PROFILE_FUNCTION();

		glDeleteBuffers(1, &m_RendererID);
	}

	void OpenGLIndexBuffer::Bind() const
	{
		RF_PROFILE_FUNCTION();

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
	}

	void OpenGLIndexBuffer::Unbind() const
	{
		RF_PROFILE_FUNCTION();

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}
}