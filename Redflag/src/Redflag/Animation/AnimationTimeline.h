#pragma once

#include "AnimationController.h"
#include <deque>
#include <set>

namespace Redflag
{
	enum class NotifyPhase { Point, Begin, End };

	struct AnimationNotice
	{
		std::string State, Name;
		NotifyPhase Phase;
		float Seconds;
		bool Interrupted = false;
	};

	/* 每个实体单独记住已播放的时间和开启的窗口。读取一条通知后，下一次读取会得到下一条。 */
	class AnimationTimeline
	{
	public:
		bool Play(const AnimationState& state, float clipSeconds);
		void Advance(float seconds);
		void Cancel();
		bool Read(AnimationNotice& result);
		bool IsOpen(const std::string& name) const { return m_Open.count(name) != 0; }
		const std::string& State() const { return m_State; }
		bool Playing() const { return !m_State.empty() && (m_Loop || m_Time < m_Length); }
		float Time() const { return m_Time; }

	private:
		void Emit(const AnimationNotice& mark, bool interrupted = false);
		void StartLoop();
		std::string m_State;
		std::vector<AnimationNotice> m_Marks;
		std::deque<AnimationNotice> m_Queue;
		std::set<std::string> m_Open;
		float m_Time = 0, m_Length = 0;
		bool m_Loop = false;
	};
}
