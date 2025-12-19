#pragma once
//接口类，只含有纯虚函数
//运行时RTTI，根据特定图形API生成特定VBO和IBO

namespace Redflag
{
	// VBO - 顶点缓冲区
	class VertexBuffer
	{
	public:
		virtual ~VertexBuffer() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		static VertexBuffer* Create(float* vertices, uint32_t size);
	};

	// IBO - 索引缓冲区
	class IndexBuffer
	{
	public:
		virtual ~IndexBuffer() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;
		virtual uint32_t GetCount() const = 0;  // 获取索引数量

		// 注意：索引通常是uint32_t类型，不是float
		static IndexBuffer* Create(uint32_t* indices, uint32_t count);
	};
}