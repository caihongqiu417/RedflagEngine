#pragma once
#include "Redflag/Renderer/VertexArray.h"

namespace Redflag {
	/* 记住从哪块缓冲区读取顶点和索引，以及一个顶点里的哪些数表示位置、法线等属性。 */
	class OpenGLVertexArray : public VertexArray
	{
	public:
		OpenGLVertexArray();
		virtual ~OpenGLVertexArray();

		virtual void Bind() const override;
		virtual void Unbind() const override;

		// 按 BufferLayout 的顺序设置属性。第一项对应着色器的 location 0。
		virtual void AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer)override;
		virtual void SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer)override;

		virtual const std::vector<Ref<VertexBuffer>>& GetVertexBuffers()const override{ return m_VertexBuffers; }
		virtual const Ref<IndexBuffer>& GetIndexBuffer()const override{ return m_IndexBuffer; }
	private:
		uint32_t m_RendererID;
		std::vector<Ref<VertexBuffer>> m_VertexBuffers;
		Ref<IndexBuffer> m_IndexBuffer;
	};
}