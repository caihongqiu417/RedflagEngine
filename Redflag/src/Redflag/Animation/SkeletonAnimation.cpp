#include "rfpch.h"
#include "SkeletonAnimation.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Redflag
{
	void SkeletonAnimationComponent::Load(const std::filesystem::path& path)
	{
		Ref<AnimationClip> clip = CreateRef<AnimationClip>(AnimationClip::LoadBVH(path));
		File = std::filesystem::absolute(path).lexically_normal();
		Clip = std::move(clip);
		Seek(0.f);
	}

	void SkeletonAnimationComponent::Seek(float seconds)
	{
		Time = std::fmod(seconds, Clip->Duration());
		if (Time < 0.f) Time += Clip->Duration();
		Pose local = Clip->Sample(Time);
		// 在实体原点循环演示步态，保留骨盆的上下起伏，避免动作回到开头时整个人跳回原地。
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
