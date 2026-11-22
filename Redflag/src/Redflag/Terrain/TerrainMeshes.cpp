#include "rfpch.h"
#include "TerrainMeshes.h"

namespace Redflag
{
	Ref<TerrainMeshes> TerrainMeshes::Get(TerrainComponent& component)
	{
		const Ref<Terrain> source = component.GetTerrain();
		if (!source) { component.Meshes.reset(); return nullptr; }
		const glm::vec3 sizes(component.CellSize, component.HeightScale, component.UVScaleMeters);
		const Ref<TerrainMeshes>& previous = component.Meshes;
		const bool sameShape = previous && source == previous->m_Source && sizes == previous->m_Sizes &&
			component.ChunkCells == previous->m_ChunkCells;
		if (sameShape && previous->m_Texture == component.BaseColorTexture) return previous;

		Ref<TerrainMeshes> result = CreateRef<TerrainMeshes>();
		result->m_Source = source;
		result->m_Sizes = sizes;
		result->m_ChunkCells = component.ChunkCells;
		if (sameShape)
		{
			result->m_Chunks = previous->m_Chunks;
			result->m_Meshes = previous->m_Meshes;
		}
		else
			result->m_Chunks = CreateRef<std::vector<TerrainChunk>>(source->BuildChunks(
				component.CellSize, component.HeightScale, component.ChunkCells, component.UVScaleMeters));

		result->m_Texture = component.BaseColorTexture;
		result->m_Material = Material::Create();
		result->m_Material->Roughness = 1.f;
		if (!component.BaseColorTexture.empty())
			result->m_Material->BaseColorMap = Texture2D::Create(component.BaseColorTexture);
		component.Meshes = result;
		return result;
	}

	const std::vector<Ref<Mesh>>& TerrainMeshes::GetMeshes()
	{
		if (m_Meshes.empty())
			for (const TerrainChunk& chunk : *m_Chunks)
				m_Meshes.push_back(Mesh::Create(chunk.Vertices, chunk.Indices));
		return m_Meshes;
	}
}
