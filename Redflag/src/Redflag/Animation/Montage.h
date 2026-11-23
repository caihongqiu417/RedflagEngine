#pragma once

#include "AnimationController.h"

namespace Redflag
{
	struct MontageBlend
	{
		float Seconds = .12f;
		glm::vec2 Curve{0.f, 1.f};
		float Weight(float t) const;
	};

	struct MontageSection
	{
		std::string Name;
		float Start = 0.f, End = 1.f;
		std::string Next;
	};

	struct MontageWarp
	{
		std::string Target, Root;
		float Start = 0.f, End = 1.f;
	};

	/* 多个角色可以共用这份动作。Bones 写子树的根骨，未覆盖的骨骼继续播放角色原来的动作。 */
	struct AnimationMontage
	{
		std::string Name, Clip, Slot;
		std::vector<std::string> Bones;
		MontageBlend BlendIn, BlendOut;
		std::vector<MontageSection> Sections;
		std::vector<AnimationMarker> Events;
		std::vector<MontageWarp> WarpWindows;

		bool Load(const std::filesystem::path& file, std::string& error);
		bool Save(const std::filesystem::path& file, std::string& error) const;
		bool Validate(std::string& error) const;
		const MontageSection* Find(const std::string& name) const;
		float Duration() const;
		std::optional<float> EventTime(const std::string& event) const;
	};
}
