#pragma once

#include "Redflag/Renderer/RHI/RenderDevice.h"

namespace Redflag
{
	class OpenGLRenderDevice : public RenderDevice
	{
	public:
		Ref<MeshGeometry> CreateMesh(const std::vector<MeshVertex>& vertices,
			const std::vector<uint32_t>& indices) override;

		Ref<SampledTexture> CreateTexture(uint32_t width, uint32_t height, const void* rgba8) override;
	};
}
