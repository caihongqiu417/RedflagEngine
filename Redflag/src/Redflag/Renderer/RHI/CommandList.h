#pragma once

#include "Redflag/Renderer/RHI/SurfaceMaterial.h"

namespace Redflag
{
	struct MeshGeometry;

	/* 场景先选好要画的网格和材质，命令列表把这一次绘制交给当前后端。 */
	class CommandList
	{
	public:
		virtual ~CommandList() = default;

		/// <summary>按索引绘制网格。拾取时写实体编号，光照阶段读取同一份网格的底色和表面朝向。</summary>
		/// <param name="texture">底色贴图。传空引用时，网格直接使用材质颜色。</param>
		/// <param name="transform">从网格的局部坐标变到世界坐标，模型部件的变换也应算在其中。</param>
		virtual void DrawIndexed(const Ref<MeshGeometry>& mesh, const Ref<SampledTexture>& texture,
			const glm::mat4& viewProjection, const glm::mat4& transform,
			const glm::vec4& color, int entityID, const SurfaceMaterial& surface = {}) = 0;
	};
}
