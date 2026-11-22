#pragma once

#include "BehaviorTree.h"
#include "NpcMemory.h"
#include "Redflag/Animation/AnimationController.h"

namespace Redflag
{
	struct NpcIntent
	{
		std::string Key, Description, Target, Item, Reply;
	};

	struct NpcDefinition
	{
		std::string Name, Instructions, Model, Voice, IdleClip, WalkClip;
		float Sight = 18.0f, Speed = 2.0f, Reach = 2.0f;
		float EyeHeight = 1.55f, EyeForward = .28f, VerticalFov = 65.f; // 眼睛相对角色原点的高度和前移量（米），以及垂直视角（度）。
		float TurnSpeed = 270.0f; // 每秒最多转过多少度，适用于接近目标和面对队友。
		bool ObserveWorld = true;
		bool Remember = true; // 提问时附上这个角色记在黑板里的事情。
		MemoryPolicy Memory;
		std::vector<NpcIntent> Intents;
		std::filesystem::path TreeFile;
		std::filesystem::path AnimatorFile;
		AnimationController Controller;
		BehaviorTree Tree;

		bool Load(const std::filesystem::path& path, std::string& error);
	};
}
