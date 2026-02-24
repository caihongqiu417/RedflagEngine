#include "rfpch.h"
#include "Framebuffer.h"

#include"Redflag/Renderer/Renderer.h"

#include"Platform/OpenGL/OpenGLFramebuffer.h"

namespace Redflag {

	Ref<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: RF_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
			case RendererAPI::API::OpenGL: return CreateRef<OpenGLFramebuffer>(spec);
		}

		RF_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}