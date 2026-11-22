#pragma once
#include "Redflag/Renderer/RHI/CommandList.h"

namespace Redflag
{
	class Shader;

	/* 在已选的帧缓冲上绘制底色和实体编号，与 Vulkan 的基础材质使用同样的输入。 */
	class OpenGLCommandList final : public CommandList
	{
	public:
		OpenGLCommandList();

		void DrawIndexed(const Ref<MeshGeometry>& mesh, const Ref<SampledTexture>& texture,
			const glm::mat4& viewProjection, const glm::mat4& transform,
			const glm::vec4& color, int entityID, const SurfaceMaterial& surface = {},
			const std::vector<glm::mat4>* instances = nullptr) override;
	private:
		Ref<Shader> m_Shader;
		Ref<SampledTexture> m_WhiteTexture;
	};
}
