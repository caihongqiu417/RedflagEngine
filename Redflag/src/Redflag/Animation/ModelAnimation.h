#pragma once

#include "SkinnedMesh.h"
#include <filesystem>
#include <memory>

namespace Redflag
{
	/* 模型里的节点、逆绑定矩阵和动作可以让多个角色共用。Sample 只计算这一次姿势，不改变资源。 */
	class ModelAnimation
	{
	public:
		struct Clip { std::string Name; float Duration; };
		struct Part { glm::mat4 Transform{1.f}; std::vector<MeshVertex> Vertices; };
		struct Limb { std::string Upper, Lower, Tip; };
		struct NodePose
		{
			glm::vec3 Position, Scale;
			glm::quat Rotation;
		};

		static Ref<ModelAnimation> Load(const std::filesystem::path& path);
		~ModelAnimation();
		const std::vector<Clip>& GetClips() const { return m_Clips; }
		// 蒙皮部件的顶点会更新，三角形编号始终相同。刚性部件直接画原模型，这里返回空数组。
		const std::vector<uint32_t>& GetIndices(size_t part) const;
		std::vector<Part> Sample(size_t clip, float seconds) const;

		/// <summary>取得各节点相对父节点的位置、旋转和缩放。多个动作共用这套骨架。</summary>
		void SamplePose(size_t clip, float seconds, std::vector<NodePose>& local) const;
		// 一个循环里运动根沿地面走了多远。以模型为坐标系，包含骨架父节点的旋转和缩放。
		float GetStrideLength(size_t clip, const std::string& root) const;
		/// <summary>按节点从上一帧的姿势过渡到当前姿势。旋转沿四元数的短弧插值。</summary>
		/// <param name="weight">0 保留来源姿势，1 到达新动作。</param>
		static void BlendPose(const std::vector<NodePose>& from, float weight, std::vector<NodePose>& to);
		void GetModelTransforms(const std::vector<NodePose>& local, std::vector<glm::mat4>& world) const;
		/// <summary>按模型的关节层级连接两端。编辑器用这些模型坐标画出指定时刻的骨架。</summary>
		std::vector<std::pair<glm::vec3, glm::vec3>> SampleBones(size_t clip, float seconds) const;
		/// <summary>将运动根留在模型的初始位置。角色本身由物理移动时，剪辑里的起跳高度不会再叠加一次。</summary>
		void PinMovementRoot(std::vector<NodePose>& local, const std::string& node) const;
		/// <summary>在当前模型姿势中把脚踝或手腕移向目标。未指定或不属于同一肢体的关节不会修改姿势。</summary>
		bool SolveLimb(std::vector<NodePose>& local, const std::vector<glm::mat4>& world,
			const Limb& limb, glm::vec3 target, bool keepTipRotation = false) const;
		std::vector<Part> BuildParts(const std::vector<NodePose>& local, std::vector<glm::mat4>& modelSpace) const;
		bool GetNodeTransform(const std::string& name, const std::vector<glm::mat4>& modelSpace, glm::mat4& result) const;
		bool GetRestTransform(const std::string& name, glm::mat4& result) const;

	private:
		struct Data;
		ModelAnimation();
		std::unique_ptr<Data> m_Data;
		std::vector<Clip> m_Clips;
	};
}
