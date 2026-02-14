#pragma once
#include"Core/Log.h"
//接口类，只含有纯虚函数
//运行时RTTI，根据特定图形API生成特定VBO和IBO

namespace Redflag
{
	enum class ShaderDataType
	{
		None = 0,Float,Float2,Float3,Float4,Mat3,Mat4,Int,Int2,Int3,Int4,Bool
	};

	static uint32_t ShaderDataTypeSize(ShaderDataType type)
	{
		switch (type)
		{
			case ShaderDataType::Float:   return 4;
			case ShaderDataType::Float2:  return 4*2;
			case ShaderDataType::Float3:  return 4*3;
			case ShaderDataType::Float4:  return 4*4;
			case ShaderDataType::Mat3:    return 4*3 * 3;   
			case ShaderDataType::Mat4:    return 4*4 * 4;    
			case ShaderDataType::Int:     return 4;
			case ShaderDataType::Int2:    return 4*2;
			case ShaderDataType::Int3:    return 4*3;
			case ShaderDataType::Int4:    return 4*4;
			case ShaderDataType::Bool:    return 1;
		}

		RF_CORE_ASSERT(false, "Unknown ShaderType");
	}


	// 缓冲区元素结构体 - 描述顶点缓冲区中的一个属性
	// 每个BufferElement对应顶点数据中的一个字段（如位置、颜色等）
	struct BufferElement
	{
		std::string Name;
		ShaderDataType Type;
		uint32_t Size;
		uint32_t Offset;
		bool Normalized;

		BufferElement() = default;

		BufferElement(ShaderDataType type,const std::string& name,bool normalized=false)
			:Type(type),Name(name),Size(ShaderDataTypeSize(type)), Offset(0),Normalized(false)
		{

		}

		uint32_t GetComponentCount()const
		{
			switch (Type)
			{
				case ShaderDataType::Float:   return 1;
				case ShaderDataType::Float2:  return 2;
				case ShaderDataType::Float3:  return 3;
				case ShaderDataType::Float4:  return 4;
				case ShaderDataType::Mat3:    return 3 * 3;
				case ShaderDataType::Mat4:    return 4 * 4;
				case ShaderDataType::Int:     return 1;
				case ShaderDataType::Int2:    return 2;
				case ShaderDataType::Int3:    return 3;
				case ShaderDataType::Int4:    return 4;
				case ShaderDataType::Bool:    return 1;
			}
		}
	};

	// 缓冲区布局类 - 描述整个顶点缓冲区的结构
	// 包含多个BufferElement，定义顶点的完整内存布局
	class BufferLayout
	{
	public:
		BufferLayout() {}
		BufferLayout(const std::initializer_list<BufferElement>& elements) 
			:m_Elements(elements)
		{
			CalculateOffsetsAndStride();
		}

		inline uint32_t GetStride()const { return m_Stride; }
		inline const std::vector<BufferElement>& GetElements() const { return m_Elements; }
		
		std::vector<BufferElement>::iterator begin() { return m_Elements.begin(); }
		std::vector<BufferElement>::iterator end() { return m_Elements.end(); }
		std::vector<BufferElement>::const_iterator begin()const { return m_Elements.begin(); }
		std::vector<BufferElement>::const_iterator end()const { return m_Elements.end(); }
	private:
		void CalculateOffsetsAndStride()
		{
			uint32_t offset = 0;	//该属性在单个顶点内部的位置
			m_Stride = 0;			//跳过整个顶点到下一个顶点的距离-
			for (auto& element : m_Elements)
			{
				// 1. Position: offset=0, stride+=12 → stride=12
				// 2. Color: offset=12, stride+=16 → stride=28  
				// 3. TexCoord: offset=28, stride+=8 → stride=36
				element.Offset = offset;
				offset +=element.Size;
				m_Stride += element.Size;
			}
		}
	private:
		std::vector<BufferElement> m_Elements;
		uint32_t m_Stride = 0;
	};
	
	
	
	
	
	// VBO - 顶点缓冲区
	class VertexBuffer
	{
	public:
		virtual ~VertexBuffer() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual void SetLayout(const BufferLayout& layout) = 0;
		virtual const BufferLayout& GetLayout() const = 0;

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