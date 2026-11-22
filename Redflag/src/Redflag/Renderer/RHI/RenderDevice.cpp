#include "rfpch.h"
#include "RenderDevice.h"
#include "Platform/OpenGL/OpenGLRenderDevice.h"
#include "Redflag/Renderer/RendererAPI.h"
#include <stdexcept>

namespace Redflag
{
	static RenderDevice* s_Device = nullptr;

	RenderDevice& RenderDevice::Get()
	{
		if (s_Device)
			return *s_Device;

		// 旧的 OpenGL 场景和测试也从这个入口创建几何。
		if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL)
		{
			static OpenGLRenderDevice device;
			return device;
		}
		throw std::logic_error("Create the render device before loading scene resources");
	}

	void RenderDevice::SetCurrent(RenderDevice* device)
	{
		s_Device = device;
	}
}
