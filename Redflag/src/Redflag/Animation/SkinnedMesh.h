#pragma once

#include "Skeleton.h"
#include <memory>
#include "Redflag/Renderer/Mesh.h"
#include <array>

namespace Redflag
{
	struct SkinWeights
	{
		std::array<unsigned, 4> Joints{};
		std::array<float, 4> Weights{};
	};

	class SkinnedMesh
	{
	public:
		SkinnedMesh(const Skeleton& rig, std::vector<MeshVertex> vertices,
			std::vector<SkinWeights> weights, std::vector<uint32_t> indices);
		SkinnedMesh(size_t jointCount, std::vector<MeshVertex> vertices,
			std::vector<SkinWeights> weights, std::vector<uint32_t> indices);

		/// <summary>将初始网格按各关节的姿势变形，返回模型坐标中的顶点，可交给 RenderDevice::CreateMesh。</summary>
		std::vector<MeshVertex> Deform(const Pose& modelPose) const;
		/// <param name="palette">每个矩阵将原网格顶点从绑定姿势变换到当前模型坐标。</param>
		std::vector<MeshVertex> Deform(const std::vector<glm::mat4>& palette) const;
		const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

	private:
		Pose m_Rest;
		std::vector<MeshVertex> m_Vertices;
		std::vector<SkinWeights> m_Weights;
		std::vector<uint32_t> m_Indices;
	};
}
