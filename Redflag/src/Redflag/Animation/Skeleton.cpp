#include "rfpch.h"
#include "Skeleton.h"
#include <stdexcept>

namespace Redflag
{
	int Skeleton::Find(const std::string& name) const
	{
		for (size_t i = 0; i < Joints.size(); ++i)
			if (Joints[i].Name == name)
				return static_cast<int>(i);
		return -1;
	}

	Pose Skeleton::RestPose() const
	{
		Pose pose(Joints.size());
		for (size_t i = 0; i < pose.size(); ++i)
			pose[i].Position = Joints[i].Offset;
		return pose;
	}

	Pose Skeleton::ToModelSpace(const Pose& local) const
	{
		if (local.size() != Joints.size())
			throw std::invalid_argument("Pose and skeleton have different joint counts");
		Pose model = local;
		for (size_t i = 0; i < model.size(); ++i)
		{
			int parent = Joints[i].Parent;
			if (parent < 0)
				continue;
			model[i].Position = model[parent].Position + model[parent].Rotation * local[i].Position;
			model[i].Rotation = glm::normalize(model[parent].Rotation * local[i].Rotation);
		}
		return model;
	}

	void Skeleton::SolveTwoBone(Pose& local, int end, glm::vec3 target, glm::vec3 pole) const
	{
		if (end < 0 || static_cast<size_t>(end) >= Joints.size() || Joints[end].Parent < 0 || Joints[Joints[end].Parent].Parent < 0)
			throw std::invalid_argument("Two-bone IK needs an end joint with two ancestors");
		int middle = Joints[end].Parent;
		int start = Joints[middle].Parent;
		Pose model = ToModelSpace(local);
		glm::vec3 origin = model[start].Position;
		glm::vec3 first = model[middle].Position - origin;
		glm::vec3 second = model[end].Position - model[middle].Position;
		float a = glm::length(first), b = glm::length(second);
		constexpr float minLength = 0.00001f; // 两点相距不到 0.01 毫米时，二者不再定义稳定的方向。
		if (a < minLength || b < minLength)
			return;
		float distance = glm::distance(target, origin);
		glm::vec3 axis = distance < minLength ? first / a : (target - origin) / distance;
		glm::vec3 bend = pole - origin - axis * glm::dot(pole - origin, axis);
		if (glm::length(bend) < minLength)
			bend = glm::cross(axis, std::abs(axis.y) < 0.9f ? glm::vec3(0,1,0) : glm::vec3(1,0,0));

		// 两根骨头与目标连成三角形。沿目标方向量出膝盖的位置，再朝 pole 一侧补上高度。
		distance = glm::clamp(distance, glm::max(std::abs(a - b), minLength), a + b);
		float along = (a*a + distance*distance - b*b) / (2.0f * distance);
		float height = std::sqrt(glm::max(0.0f, a*a - along*along));
		glm::vec3 knee = origin + along * axis + height * glm::normalize(bend);
		glm::vec3 tip = origin + distance * axis;
		glm::quat turn(first / a, glm::normalize(knee - origin));
		glm::quat upper = turn * model[start].Rotation;
		int parent = Joints[start].Parent;
		local[start].Rotation = parent < 0 ? upper : glm::inverse(model[parent].Rotation) * upper;
		glm::quat lower = glm::quat(glm::normalize(turn * second), glm::normalize(tip - knee)) * turn * model[middle].Rotation;
		local[middle].Rotation = glm::normalize(glm::inverse(upper) * lower);
	}
}
