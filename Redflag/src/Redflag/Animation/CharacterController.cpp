#include "rfpch.h"
#include "CharacterController.h"
#include <stdexcept>

namespace Redflag
{
	namespace
	{
		float Heading(const glm::quat& rotation)
		{
			glm::vec3 forward = rotation * glm::vec3(0,0,1);
			return std::atan2(forward.x, forward.z);
		}
	}

	CharacterController::CharacterController(const AnimationClip& idle, const AnimationClip& walk)
		: m_Idle(idle), m_Walk(walk)
	{
		if (idle.Rig.Joints.size() != walk.Rig.Joints.size())
			throw std::invalid_argument("Idle and walk need the same skeleton");
		for (size_t i = 0; i < idle.Rig.Joints.size(); ++i)
		{
			const Joint& a = idle.Rig.Joints[i];
			const Joint& b = walk.Rig.Joints[i];
			if (a.Parent != b.Parent || a.Name != b.Name || glm::distance(a.Offset, b.Offset) > 0.0001f)
				throw std::invalid_argument("Idle and walk joint mismatch: " + a.Name);
		}
		m_IdleHeading = Heading(idle.Frames.front()[0].Rotation);
		m_WalkHeading = Heading(walk.Frames.front()[0].Rotation);
		if (walk.Frames.size() < 2)
			throw std::invalid_argument("Walk needs two or more frames");
		glm::vec3 travel = walk.Frames.back()[0].Position - walk.Frames.front()[0].Position;
		m_ClipSpeed = glm::length(glm::vec2(travel.x, travel.z)) / (walk.Duration() - walk.FrameTime);
		if (!(m_ClipSpeed > 0.0f))
			throw std::invalid_argument("Walk needs root travel to measure stride speed");
	}

	Pose CharacterController::Update(float seconds, glm::vec2 velocity)
	{
		float speed = glm::length(velocity);
		m_Position += velocity * seconds;
		m_Clock += seconds;
		m_WalkTime += seconds * speed / m_ClipSpeed;
		// 开始或停止走路时，用约 0.12 秒把身体从上一种动作转向下一种。
		constexpr float blendTime = 0.12f;
		float response = 1.0f - std::exp(-seconds / blendTime);
		m_WalkWeight = glm::mix(m_WalkWeight, glm::min(speed / m_ClipSpeed, 1.0f), response);
		if (speed > 0.0f)
		{
			float angle = std::atan2(velocity.x, velocity.y) - m_Heading;
			m_Heading += std::atan2(std::sin(angle), std::cos(angle)) * response;
		}

		Pose pose = m_Idle.get().Sample(m_Clock);
		Pose walk = m_Walk.get().Sample(m_WalkTime);
		const glm::vec3 up(0,1,0);
		pose[0].Rotation = glm::angleAxis(m_Heading - m_IdleHeading, up) * pose[0].Rotation;
		walk[0].Rotation = glm::angleAxis(m_Heading - m_WalkHeading, up) * walk[0].Rotation;
		for (size_t i = 0; i < pose.size(); ++i)
		{
			pose[i].Position = glm::mix(pose[i].Position, walk[i].Position, m_WalkWeight);
			pose[i].Rotation = glm::slerp(pose[i].Rotation, walk[i].Rotation, m_WalkWeight);
		}
		// 保留动作中的上下起伏。水平位移由玩家指定的速度积分，循环播放时角色不会跳回起点。
		pose[0].Position.x = m_Position.x;
		pose[0].Position.z = m_Position.y;
		return pose;
	}
}
