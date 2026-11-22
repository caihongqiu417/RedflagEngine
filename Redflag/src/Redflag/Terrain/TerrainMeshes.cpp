#include "rfpch.h"
#include "TerrainMeshes.h"
#include "Redflag/Core/Application.h"

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
		{
			// 维修站的 129×129 高度图重算时能用上已有的工作线程。首次加载单为这幅图创建线程并未加快就绪。
			Application* app = Application::Current();
			JobSystem* jobs = app ? app->ExistingJobs() : nullptr;
			const bool measuredSize = source->GetWidth() >= 129 && source->GetDepth() >= 129;
			result->m_Chunks = CreateRef<std::vector<TerrainChunk>>(source->BuildChunks(
				component.CellSize, component.HeightScale, component.ChunkCells, component.UVScaleMeters,
				measuredSize && component.ChunkCells <= 32 ? jobs : nullptr));
		}

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
