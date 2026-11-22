#include "rfpch.h"
#include "SkinnedMesh.h"
#include <stdexcept>

namespace Redflag
{
	SkinnedMesh::SkinnedMesh(const Skeleton& rig, std::vector<MeshVertex> vertices,
		std::vector<SkinWeights> weights, std::vector<uint32_t> indices)
		: m_Rest(rig.ToModelSpace(rig.RestPose())), m_Vertices(std::move(vertices)),
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
				if (weight < 0.0f || (weight != 0.0f && influence.Joints[j] >= m_Rest.size()))
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
		Pose changes = modelPose;
		for (size_t i = 0; i < changes.size(); ++i)
			changes[i].Position -= changes[i].Rotation * m_Rest[i].Position;

		std::vector<MeshVertex> vertices = m_Vertices;
		for (size_t i = 0; i < vertices.size(); ++i)
		{
			const MeshVertex& rest = m_Vertices[i];
			MeshVertex& vertex = vertices[i];
			vertex.Position = vertex.Normal = vertex.Tangent = glm::vec3(0.0f);
			for (size_t j = 0; j < 4; ++j)
			{
				float weight = m_Weights[i].Weights[j];
				if (weight == 0.0f)
					continue;
				const JointPose& joint = changes[m_Weights[i].Joints[j]];
				vertex.Position += weight * (joint.Position + joint.Rotation * rest.Position);
				vertex.Normal += weight * (joint.Rotation * rest.Normal);
				vertex.Tangent += weight * (joint.Rotation * rest.Tangent);
			}
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
