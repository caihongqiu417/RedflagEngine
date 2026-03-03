#pragma once

#include"Redflag/Core/Core.h"

namespace Redflag {
	
	enum class FramebufferTextureFormat
	{
		None = 0,

		//Color
		RED_INTEGER,
		RGBA8,

		//Depth
		DEPTH24STENCIL8,

		//Defaults
		Depth = DEPTH24STENCIL8
	};

	//单个附件的规格
	struct FramebufferTextureSpecification
	{
		FramebufferTextureSpecification() = default;
		FramebufferTextureSpecification(FramebufferTextureFormat format)
			:TextureFormat(format){ }
		
		FramebufferTextureFormat TextureFormat=FramebufferTextureFormat::None;
		//TODO:filtering,wrap
	};

	// 多个附件的规格
	struct FramebufferAttachmentSpecification
	{
		FramebufferAttachmentSpecification() = default;
		FramebufferAttachmentSpecification(std::initializer_list<FramebufferTextureSpecification> attachments)
			:Attachments(attachments){ }

		std::vector<FramebufferTextureSpecification> Attachments;

	};

	//整个缓冲区的规格
	struct FramebufferSpecification
	{
		uint32_t Width, Height;

		FramebufferAttachmentSpecification Attachments;

		uint32_t Samples = 1; //不开启MSAA
		
		bool SwapChainTarget = false; // false: 用户自己创建的帧缓冲区（内容需手动拷贝到交换链才能显示) true:  直接使用交换链的后缓冲区（调用Present后自动显示到屏幕）
	};

	class Framebuffer
	{
	public:
		virtual ~Framebuffer() = default;

		virtual void Bind() = 0;
		virtual void Unbind() = 0;

		virtual void Resize(uint32_t width,uint32_t height)=0;
		virtual int ReadPixel(uint32_t attachmentIndex, int x, int y) = 0;

		virtual void ClearAttachment(uint32_t attachmentIndex,int value)=0;
		
		virtual uint32_t GetColorAttachmentRendererID(uint32_t index=0) const = 0;

		virtual const FramebufferSpecification& GetSpecifictaion() const = 0;

		static Ref<Framebuffer> Create(const FramebufferSpecification& spec);
	};
}