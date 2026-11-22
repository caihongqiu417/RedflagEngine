#pragma once

#include "Redflag/Renderer/Mesh.h"

namespace Redflag
{
	struct OpenGLMeshGeometry final : MeshGeometry
	{
		Ref<VertexArray> Array;
	};
}
