#include "rfpch.h"
#include "OpenGLCommandList.h"
#include "OpenGLMeshGeometry.h"
#include "Redflag/Renderer/RHI/RenderDevice.h"
#include "Redflag/Renderer/RenderCommand.h"
#include "Redflag/Renderer/Shader.h"
#include "Redflag/Renderer/Texture.h"

namespace Redflag
{
	OpenGLCommandList::OpenGLCommandList()
	{
		m_Shader = Shader::Create("assets/shaders/RHI_Mesh.glsl");

		// 没有贴图的物体用白色相乘，保留材质自己的底色。
		const uint32_t white = 0xffffffff;
		m_WhiteTexture = RenderDevice::Get().CreateTexture(1, 1, &white);
	}

	void OpenGLCommandList::DrawIndexed(const Ref<MeshGeometry>& mesh, const Ref<SampledTexture>& texture,
		const glm::mat4& viewProjection, const glm::mat4& transform,
		const glm::vec4& color, int entityID, const SurfaceMaterial& surface, const std::vector<glm::mat4>* instances)
	{
		if (instances)
		{
			for (const glm::mat4& instance : *instances)
				DrawIndexed(mesh, texture, viewProjection, transform * instance, color, entityID, surface);
			return;
		}
		m_Shader->Bind();
		m_Shader->SetMat4("u_ViewProjectionModel", viewProjection * transform);
		m_Shader->SetFloat4("u_Color", color);
		m_Shader->SetInt("u_EntityID", entityID);
		m_Shader->SetInt("u_BaseColorMap", 0);

		// 先选底色贴图和网格，下面的 DrawIndexed 就会用它们画这个物体。
		dynamic_cast<Texture2D&>(*(texture ? texture : m_WhiteTexture)).Bind();
		const auto& vertices = static_cast<const OpenGLMeshGeometry&>(*mesh).Array;
		vertices->Bind();
		RenderCommand::DrawIndexed(vertices);
	}
}
