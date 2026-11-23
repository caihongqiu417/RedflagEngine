#include "rfpch.h"
#include "AnimationTimeline.h"

namespace Redflag
{
	bool AnimationTimeline::Play(const AnimationState& state, float clipSeconds)
	{
		if (state.MontageAsset || !(clipSeconds > 0.f)) return false;
		for (const auto& marker : state.Events)
			if (marker.End.value_or(marker.Start) > clipSeconds) return false;
		Cancel();
		m_State = state.ID;
		m_Length = clipSeconds;
		m_Loop = state.Loop;
		m_Time = 0.f;
		m_Marks.clear();
		for (const AnimationMarker& marker : state.Events) {
			m_Marks.push_back({state.ID,marker.Name,marker.End ? NotifyPhase::Begin : NotifyPhase::Point,marker.Start});
			if (marker.End) m_Marks.push_back({state.ID,marker.Name,NotifyPhase::End,*marker.End});
		}
		if (state.HasContact) m_Marks.push_back({state.ID,"contact",NotifyPhase::Point,state.Contact*clipSeconds});
		std::stable_sort(m_Marks.begin(),m_Marks.end(),[](const AnimationNotice& a,const AnimationNotice& b) {
			return a.Seconds < b.Seconds;
		});
		StartLoop();
		return true;
	}

	void AnimationTimeline::StartLoop()
	{
		for (const AnimationNotice& mark : m_Marks)
			if (mark.Seconds==0.f) Emit(mark);
	}

	void AnimationTimeline::Emit(const AnimationNotice& mark, bool interrupted)
	{
		if (mark.Phase == NotifyPhase::Begin) m_Open.insert(mark.Name);
		if (mark.Phase == NotifyPhase::End) m_Open.erase(mark.Name);
		m_Queue.push_back(mark);
		m_Queue.back().Interrupted = interrupted;
	}

	void AnimationTimeline::Advance(float seconds)
	{
		if (m_State.empty() || seconds <= 0.f) return;
		// 一帧可越过多个标记甚至整轮动作。先发完本轮末尾的事件，再发下一轮零秒的事件。
		while (seconds > 0.f) {
			const float remaining = m_Length - m_Time;
			const float next = seconds >= remaining ? m_Length : m_Time + seconds;
			for (const auto& mark : m_Marks)
				if (mark.Seconds > m_Time && mark.Seconds <= next) Emit(mark);
			m_Time = next;
			if (next < m_Length || !m_Loop) break;
			seconds -= remaining;
			m_Time = 0.f;
			StartLoop();
		}
	}

	void AnimationTimeline::Cancel()
	{
		while (!m_Open.empty()) {
			const AnimationNotice end{m_State,*m_Open.begin(),NotifyPhase::End,m_Time};
			Emit(end,true);
		}
		m_State.clear();
	}

	bool AnimationTimeline::Read(AnimationNotice& result)
	{
		if (m_Queue.empty()) return false;
		result = std::move(m_Queue.front());
		m_Queue.pop_front();
		return true;
	}
}
