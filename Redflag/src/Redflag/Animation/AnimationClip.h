#pragma once

#include "Skeleton.h"
#include <filesystem>

namespace Redflag
{
	class AnimationClip
	{
	public:
		/* BVH 文件按自己的顺序列出每个关节的平移和转角。课程动作以米为单位。 */
		static AnimationClip LoadBVH(const std::filesystem::path& path, float metersPerUnit = 1.0f);
		Pose Sample(float seconds) const;
		float Duration() const { return FrameTime * static_cast<float>(Frames.size()); }

		Skeleton Rig;
		float FrameTime = 0.0f;
		std::vector<Pose> Frames;
	};
}
