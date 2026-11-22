#pragma once

#include "AnimationClip.h"

namespace Redflag
{
	/* 两段动作使用相同的骨架，调用者需在控制器使用期间保留它们。 */
	class CharacterController
	{
	public:
		CharacterController(const AnimationClip& idle, const AnimationClip& walk);

		/// <param name="velocity">世界坐标中的水平速度（米/秒），零表示停步。</param>
		Pose Update(float seconds, glm::vec2 velocity);
		glm::vec2 GetPosition() const { return m_Position; }

	private:
		const AnimationClip& m_Idle;
		const AnimationClip& m_Walk;
		glm::vec2 m_Position{0.0f};
		float m_IdleHeading, m_WalkHeading, m_ClipSpeed;
		float m_Heading = 0.0f, m_Clock = 0.0f, m_WalkTime = 0.0f, m_WalkWeight = 0.0f;
	};
}
