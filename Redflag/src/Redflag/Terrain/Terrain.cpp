#include "rfpch.h"
#include "TerrainComponent.h"
#include <limits>
// 高度图总是从顶行开始读取。使用独立的 PNG 解码器，不改变材质贴图的翻转设置。
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Redflag
{
	Ref<Terrain> Terrain::Load(const std::filesystem::path& path)
	{
		// 路径交给 Windows 的文件接口，stb 只读取文件里的像素，以便项目目录使用中文。
		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		const std::streamoff length = stream.tellg();
		if (length <= 0 || length > std::numeric_limits<int>::max()) return nullptr;
		std::vector<unsigned char> bytes(static_cast<size_t>(length));
		stream.seekg(0);
		if (!stream.read(reinterpret_cast<char*>(bytes.data()), length)) return nullptr;
		int width, depth, channels;
		std::unique_ptr<stbi_us, decltype(&stbi_image_free)> pixels(
			stbi_load_16_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &depth, &channels, 1),
			stbi_image_free);
		if (!pixels || width < 2 || depth < 2 || channels != 1) return nullptr;

		Ref<Terrain> terrain = CreateRef<Terrain>();
		terrain->m_Width = width;
		terrain->m_Depth = depth;
		terrain->m_Heights.assign(pixels.get(), pixels.get() + size_t(width) * depth);
		return terrain;
	}

	std::optional<float> Terrain::Sample(float x, float z, float cellSize, float heightScale) const
	{
		const auto surface = SampleSurface(x, z, cellSize, heightScale);
		if (surface) return surface->Height;
		return {};
	}

	std::optional<TerrainSurface> Terrain::SampleSurface(float x, float z, float cellSize, float heightScale) const
	{
		if (!(cellSize > 0.f && x >= 0.f && z >= 0.f && x <= (m_Width-1)*cellSize && z <= (m_Depth-1)*cellSize))
			return {};
		const float u = x / cellSize, v = z / cellSize;
		const uint32_t col = std::min(static_cast<uint32_t>(u), m_Width - 2);
		const uint32_t row = std::min(static_cast<uint32_t>(v), m_Depth - 2);
		const float dx = u - col, dz = v - row;
		const float a = GetHeight(col, row), b = GetHeight(col + 1, row);
		const float c = GetHeight(col, row + 1), d = GetHeight(col + 1, row + 1);

		// 对角线由左上角连到右下角。分别在两块三角形的平面上求高度，与绘制和碰撞一致。
		const float sx = dx >= dz ? b-a : d-c, sz = dx >= dz ? d-b : c-a;
		const float unit = heightScale / 65535.f;
		return TerrainSurface{(a + sx*dx + sz*dz) * unit, glm::normalize(glm::vec3(-sx*unit, cellSize, -sz*unit))};
	}

	std::vector<TerrainChunk> Terrain::BuildChunks(float cellSize, float heightScale, uint32_t chunkCells, float uvScaleMeters) const
	{
		std::vector<TerrainChunk> chunks;
		if (!(cellSize > 0.f && uvScaleMeters > 0.f) || !chunkCells) return chunks;
		const float unit = heightScale / 65535.f;

		for (uint32_t z = 0; z < m_Depth - 1; )
		{
			const uint32_t rows = std::min(chunkCells, m_Depth - 1 - z);
			for (uint32_t x = 0; x < m_Width - 1; )
			{
				const uint32_t cols = std::min(chunkCells, m_Width - 1 - x);
				TerrainChunk& chunk = chunks.emplace_back();
				chunk.Vertices.reserve(size_t(cols + 1) * (rows + 1));
				chunk.Indices.reserve(size_t(cols) * rows * 6);
				for (uint32_t j = z; j <= z + rows; ++j)
					for (uint32_t i = x; i <= x + cols; ++i)
					{
						// 在整幅图上求坡度。即使复制了块边界的顶点，两侧的法线也完全相同。
						const uint32_t l = i ? i-1 : i, r = std::min(i+1, m_Width-1);
						const uint32_t t = j ? j-1 : j, b = std::min(j+1, m_Depth-1);
						const float dx = (GetHeight(r,j)-GetHeight(l,j))*unit / ((r-l)*cellSize);
						const float dz = (GetHeight(i,b)-GetHeight(i,t))*unit / ((b-t)*cellSize);
						const glm::vec3 position(i*cellSize, GetHeight(i,j)*unit, j*cellSize);
						chunk.Vertices.push_back({position, glm::normalize(glm::vec3(-dx,1,-dz)),
							glm::vec2(position.x,position.z)/uvScaleMeters, glm::normalize(glm::vec3(1,dx,0))});
					}
				for (uint32_t j = 0; j < rows; ++j)
					for (uint32_t i = 0; i < cols; ++i)
					{
						const uint32_t a = j*(cols+1)+i, b = a+1, c = a+cols+1, d = c+1;
						chunk.Indices.insert(chunk.Indices.end(), {a,c,d, a,d,b});
					}
				x += cols;
			}
			z += rows;
		}
		return chunks;
	}

	Ref<Terrain> TerrainComponent::GetTerrain() const
	{
		if (Heightmap != m_LoadedPath)
		{
			m_Data = Heightmap.empty() ? nullptr : Terrain::Load(std::filesystem::u8path(Heightmap));
			m_LoadedPath = Heightmap;
		}
		return m_Data;
	}

	void TerrainComponent::Reload()
	{
		m_LoadedPath.clear();
		m_Data.reset();
	}
}
