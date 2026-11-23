#pragma once

#include "AnimationTimeline.h"

namespace Redflag
{
	class Entity;

	/* 动作表可供多个角色读取。这个对象随当前实体销毁，只保留该角色自己的事件和播放进度。 */
	struct ActorAnimator
	{
		AnimationController Controller;
		AnimationTimeline Timeline;
		std::string Clip;
		std::string MontageSlot;
		uint64_t MontageCursor = 0;
		bool LastWasMontage = false;

		bool Play(Entity actor, const std::string& state, bool restart);
		bool Read(Entity actor, AnimationNotice& notice);
		bool IsPlaying(Entity actor) const;
		bool IsOpen(Entity actor, const std::string& name) const;
	};
}
