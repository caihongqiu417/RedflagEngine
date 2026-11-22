#include "rfpch.h"
#include "SkeletonAnimation.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Redflag
{
	void SkeletonAnimationComponent::Load(const std::filesystem::path& path)
	{
		Ref<AnimationClip> clip = CreateRef<AnimationClip>(AnimationClip::LoadBVH(path));
		if (m_Idle) m_Control = CharacterController(*m_Idle, *clip);
		File = std::filesystem::absolute(path).lexically_normal();
		Clip = std::move(clip);
		Seek(0.f);
	}

	void SkeletonAnimationComponent::Seek(float seconds)
	{
		Time = std::fmod(seconds, Clip->Duration());
		if (Time < 0.f) Time += Clip->Duration();
		UpdateBones(Clip->Sample(Time));
	}

	void SkeletonAnimationComponent::SetIdleMotion(const std::filesystem::path& path)
	{
		if (path.empty())
		{
			m_Control.reset();
			m_Idle.reset();
			IdleFile.clear();
			return;
		}
		if (!Clip) throw std::invalid_argument("Select a walking BVH first");
		Ref<AnimationClip> idle = CreateRef<AnimationClip>(AnimationClip::LoadBVH(path));
		m_Control = CharacterController(*idle, *Clip);
		m_Idle = std::move(idle);
		IdleFile = std::filesystem::absolute(path).lexically_normal();
	}

	void SkeletonAnimationComponent::RestartControl()
	{
		Movement.reset();
		if (m_Idle) m_Control.emplace(*m_Idle, *Clip);
	}

	void SkeletonAnimationComponent::Move(float seconds, glm::vec2 velocity)
	{
		UpdateBones(m_Control->Update(seconds, velocity));
	}

	void SkeletonAnimationComponent::UpdateBones(Pose local)
	{
		// 水平移动交给实体的位置，骨架只保留骨盆的上下起伏。
		local[0].Position.x = local[0].Position.z = 0.f;
		const Pose pose = Clip->Rig.ToModelSpace(local);
		Bones.clear();
		for (size_t i = 1; i < pose.size(); ++i)
		{
			const glm::vec3 start = pose[Clip->Rig.Joints[i].Parent].Position;
			const glm::vec3 span = pose[i].Position - start;
			const float length = glm::length(span);
			if (length == 0.f) continue; // BVH 允许多个关节位于同一点，这里没有骨段可画。
			const glm::quat rotation(glm::vec3(0,1,0), span / length);
			const glm::mat4 center = glm::translate(glm::mat4(1), start + .5f * span);
			constexpr float width = .055f; // 米。加宽后细小骨段更容易看见，但相邻关节会连成一块。
			Bones.push_back(glm::scale(center * glm::mat4_cast(rotation), {width, length, width}));
		}
	}
}
