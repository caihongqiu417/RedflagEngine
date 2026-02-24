#pragma once

#include"Redflag/Core/Core.h"

namespace Redflag {
	
	struct FramebufferSpecification
	{
		uint32_t Width, Height;

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
		
		virtual uint32_t GetColorAttachmentRendererID() const = 0;

		virtual const FramebufferSpecification& GetSpecifictaion() const = 0;

		static Ref<Framebuffer> Create(const FramebufferSpecification& spec);
	};
}