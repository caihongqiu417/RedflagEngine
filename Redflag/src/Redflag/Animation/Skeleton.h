#pragma once

#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace Redflag
{
	struct JointPose
	{
		glm::vec3 Position{0.0f};
		glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
	};

	using Pose = std::vector<JointPose>;

	/* 父关节排在子关节前面。Offset 是关节在父坐标系中的初始位置，单位为米。 */
	struct Joint
	{
		std::string Name;
		int Parent;
		glm::vec3 Offset;
	};

	class Skeleton
	{
	public:
		std::vector<Joint> Joints;
		int Find(const std::string& name) const;
		Pose RestPose() const;
		Pose ToModelSpace(const Pose& local) const;

		/// <summary>转动末端上方的两个关节，使末端靠近 target；pole 指定肘或膝弯向哪一侧。</summary>
		/// <param name="target">目标在角色模型中的位置，单位为米。</param>
		void SolveTwoBone(Pose& local, int end, glm::vec3 target, glm::vec3 pole) const;
	};
}
