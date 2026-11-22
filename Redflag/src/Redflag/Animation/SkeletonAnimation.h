#pragma once

#include "CharacterController.h"
#include "Redflag/Core/Core.h"
#include <optional>

namespace Redflag
{
	class Mesh;
	class Material;

	/* 一个角色有一份播放进度。复制角色时可以共用只读的动作和画骨段的立方体。 */
	struct SkeletonAnimationComponent
	{
		void Load(const std::filesystem::path& path);
		void Seek(float seconds);
		void SetIdleMotion(const std::filesystem::path& path);
		void RestartControl();
		/// <summary>为已配好待机与行走动作的角色计算本帧姿势。速度沿角色自己的 X、Z 轴，单位为米/秒。</summary>
		void Move(float seconds, glm::vec2 velocity);

		std::filesystem::path File;
		Ref<const AnimationClip> Clip;
		std::filesystem::path IdleFile;
		float MoveSpeed = 1.2f; // 米/秒。按住方向键一秒，走过这段距离。
		bool HasControl() const { return m_Control.has_value(); }
		bool Playing = true;
		float Time = 0.f;
		std::vector<glm::mat4> Bones; // 每个矩阵把单位立方体放到角色模型中相邻的两个关节之间。
		Ref<Mesh> BoneMesh;
		Ref<Material> BoneMaterial;

	private:
		void UpdateBones(Pose local);
		Ref<const AnimationClip> m_Idle;
		std::optional<CharacterController> m_Control;
	};
}
