#pragma once

#include "AnimationClip.h"
#include "Redflag/Core/Core.h"

namespace Redflag
{
	class Mesh;
	class Material;

	/* 一个角色有一份播放进度。复制角色时可以共用只读的动作和画骨段的立方体。 */
	struct SkeletonAnimationComponent
	{
		void Load(const std::filesystem::path& path);
		void Seek(float seconds);

		std::filesystem::path File;
		Ref<const AnimationClip> Clip;
		bool Playing = true;
		float Time = 0.f;
		std::vector<glm::mat4> Bones; // 每个矩阵把单位立方体放到角色模型中相邻的两个关节之间。
		Ref<Mesh> BoneMesh;
		Ref<Material> BoneMaterial;
	};
}
