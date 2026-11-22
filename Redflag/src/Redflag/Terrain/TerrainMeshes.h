#pragma once

#include "TerrainComponent.h"
#include "Redflag/Renderer/Material.h"

namespace Redflag
{
	/* 高度图和尺寸决定地表形状。相机绘制与光线求交共用这里的三角形。 */
	class TerrainMeshes
	{
	public:
		static Ref<TerrainMeshes> Get(TerrainComponent& component);

		const std::vector<Ref<Mesh>>& GetMeshes();
		const Ref<const std::vector<TerrainChunk>>& GetChunks() const { return m_Chunks; }
		const Ref<Material>& GetMaterial() const { return m_Material; }

	private:
		Ref<Terrain> m_Source;
		glm::vec3 m_Sizes{};
		uint32_t m_ChunkCells = 0;
		std::string m_Texture;
		Ref<const std::vector<TerrainChunk>> m_Chunks;
		std::vector<Ref<Mesh>> m_Meshes;
		Ref<Material> m_Material;
	};
}
