#include "rfpch.h"
#include "SkinnedMesh.h"
#include <stdexcept>

namespace Redflag
{
	SkinnedMesh::SkinnedMesh(const Skeleton& rig, std::vector<MeshVertex> vertices,
		std::vector<SkinWeights> weights, std::vector<uint32_t> indices)
		: SkinnedMesh(rig.Joints.size(), std::move(vertices), std::move(weights), std::move(indices))
	{
		m_Rest = rig.ToModelSpace(rig.RestPose());
	}

	SkinnedMesh::SkinnedMesh(size_t jointCount, std::vector<MeshVertex> vertices,
		std::vector<SkinWeights> weights, std::vector<uint32_t> indices)
		: m_Vertices(std::move(vertices)),
		  m_Weights(std::move(weights)), m_Indices(std::move(indices))
	{
		if (m_Weights.size() != m_Vertices.size())
			throw std::invalid_argument("Each vertex needs skin weights");
		for (const SkinWeights& influence : m_Weights)
		{
			float sum = 0.0f;
			for (size_t j = 0; j < influence.Weights.size(); ++j)
			{
				float weight = influence.Weights[j];
				if (weight < 0.0f || (weight != 0.0f && influence.Joints[j] >= jointCount))
					throw std::invalid_argument("Invalid skin influence");
				sum += weight;
			}
			if (!(std::abs(sum - 1.0f) < 0.0001f))
				throw std::invalid_argument("Skin weights must sum to one");
		}
	}

	std::vector<MeshVertex> SkinnedMesh::Deform(const Pose& modelPose) const
	{
		if (modelPose.size() != m_Rest.size())
			throw std::invalid_argument("Skin and pose have different joint counts");
		// 先将每块骨头从初始姿势转到当前姿势。初始 BVH 姿势各关节的旋转均为单位四元数。
		std::vector<glm::mat4> palette(modelPose.size());
		for (size_t i = 0; i < modelPose.size(); ++i) {
			palette[i] = glm::mat4_cast(modelPose[i].Rotation);
			palette[i][3] = glm::vec4(modelPose[i].Position - modelPose[i].Rotation * m_Rest[i].Position, 1);
		}
		return Deform(palette);
	}

	std::vector<MeshVertex> SkinnedMesh::Deform(const std::vector<glm::mat4>& palette) const
	{
		std::vector<MeshVertex> vertices = m_Vertices;
		for (size_t i = 0; i < vertices.size(); ++i)
		{
			const MeshVertex& rest = m_Vertices[i];
			MeshVertex& vertex = vertices[i];
			glm::mat4 skin(0.f);
			for (size_t j = 0; j < 4; ++j)
			{
				float weight = m_Weights[i].Weights[j];
				if (weight == 0.0f)
					continue;
				skin += weight * palette.at(m_Weights[i].Joints[j]);
			}
			vertex.Position = skin * glm::vec4(rest.Position, 1.f);
			const glm::mat3 linear(skin);
			// 法线保持垂直于变形后的切平面。余因子写法也能处理线性混合后某个方向被压平的姿势。
			const glm::mat3 cofactor(glm::cross(linear[1], linear[2]), glm::cross(linear[2], linear[0]), glm::cross(linear[0], linear[1]));
			vertex.Normal = cofactor * rest.Normal;
			if (glm::determinant(linear) < 0.f) vertex.Normal = -vertex.Normal;
			vertex.Tangent = linear * rest.Tangent;
			float normalLength = glm::length(vertex.Normal);
			if (normalLength > 0.0f)
				vertex.Normal /= normalLength;
			vertex.Tangent -= vertex.Normal * glm::dot(vertex.Normal, vertex.Tangent);
			float tangentLength = glm::length(vertex.Tangent);
			if (tangentLength > 0.0f)
				vertex.Tangent /= tangentLength;
		}
		return vertices;
	}
}
