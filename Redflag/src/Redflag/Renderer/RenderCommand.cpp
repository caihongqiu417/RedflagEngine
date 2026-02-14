#include"rfpch.h"
#include"RenderCommand.h"

#include"Platform/OpenGL/OpenGLRendererAPI.h"

namespace Redflag {

	RendererAPI* RenderCommand::s_RendererAPI = new OpenGLRendererAPI;

	
}