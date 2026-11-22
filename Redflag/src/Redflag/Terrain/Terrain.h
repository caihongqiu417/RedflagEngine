#pragma once

#include "Redflag/Renderer/Mesh.h"
#include <filesystem>
#include <optional>

namespace Redflag
{
	class JobSystem;
	struct TerrainSurface
	{
		float Height;
		glm::vec3 Normal;
	};

	struct TerrainChunk
	{
		std::vector<MeshVertex> Vertices;
		std::vector<uint32_t> Indices;
	};

	/* 一幅灰度图保存地面的高低。图中的列向右对应 +X，行向下对应 +Z。 */
	class Terrain
	{
	public:
		static Ref<Terrain> Load(const std::filesystem::path& path);
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetDepth() const { return m_Depth; }
		uint16_t GetHeight(uint32_t x, uint32_t z) const { return m_Heights[size_t(z) * m_Width + x]; }

		/// <param name="cellSize">相邻两个采样点之间的水平距离，单位为米。</param>
		/// <param name="heightScale">灰度由黑变为白对应的高度差，单位为米。</param>
		/// <returns>指定模型坐标处的三角形表面高度。地形范围外返回空。</returns>
		std::optional<float> Sample(float x, float z, float cellSize, float heightScale) const;
		std::optional<TerrainSurface> SampleSurface(float x, float z, float cellSize, float heightScale) const;
		std::vector<TerrainChunk> BuildChunks(float cellSize, float heightScale, uint32_t chunkCells,
			float uvScaleMeters = 1.f) const;
		std::vector<TerrainChunk> BuildChunks(float cellSize, float heightScale, uint32_t chunkCells,
			float uvScaleMeters, JobSystem* jobs) const;

	private:
		void BuildChunk(TerrainChunk& chunk, uint32_t x, uint32_t z, uint32_t chunkCells,
			float cellSize, float heightScale, float uvScaleMeters) const;
		uint32_t m_Width = 0, m_Depth = 0;
		std::vector<uint16_t> m_Heights;
	};
}
