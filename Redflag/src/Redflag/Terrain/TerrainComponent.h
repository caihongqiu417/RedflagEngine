#pragma once

#include "Terrain.h"

namespace Redflag
{
	class TerrainMeshes;
	struct TerrainComponent
	{
		std::string Heightmap, BaseColorTexture;
		float CellSize = 1.f, HeightScale = 20.f, UVScaleMeters = 1.f;
		uint32_t ChunkCells = 32;
		bool Collision = true;
		glm::vec4 Tint{1.f};
		Ref<TerrainMeshes> Meshes;

		Ref<Terrain> GetTerrain() const;
		void Reload();

	private:
		mutable std::string m_LoadedPath;
		mutable Ref<Terrain> m_Data;
	};
}
