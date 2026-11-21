#pragma once

#include "Redflag/Renderer/Buffer.h"

namespace Redflag
{
	/* 保存顶点数据。BufferLayout 描述一个顶点包含哪些字段，顶点数组按这份说明读取。 */
	class OpenGLVertexBuffer :public VertexBuffer
	{
	public:
		OpenGLVertexBuffer(uint32_t size); // 分配 size 字节，内容由 SetData 上传
		OpenGLVertexBuffer(float* vertices, uint32_t size);
		virtual ~OpenGLVertexBuffer();

		virtual void Bind()const;
		virtual void Unbind() const;

		virtual void SetData(const void* data, uint32_t size) override;

		// ---- 顶点布局 ----
		virtual const BufferLayout& GetLayout() const override { return m_Layout; }
		virtual void SetLayout(const BufferLayout& layout) override { m_Layout = layout; }
	private:
		uint32_t m_RendererID;
		BufferLayout m_Layout;
	};

	/* 保存绘制时取顶点的顺序。每个索引是一个 uint32，count 记录索引的个数。 */
	class OpenGLIndexBuffer : public IndexBuffer
	{
	public:
		OpenGLIndexBuffer(uint32_t* indices, uint32_t count);
		virtual ~OpenGLIndexBuffer();

		virtual void Bind() const override;
		virtual void Unbind() const override;
		virtual uint32_t GetCount() const override { return m_Count; }


	private:
		uint32_t m_RendererID;
		uint32_t m_Count;  // 索引数量
	};
}