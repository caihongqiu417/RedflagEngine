#pragma once

#include "Montage.h"
#include "ModelAnimation.h"
#include "AnimationTimeline.h"
#include <functional>
#include <map>

namespace Redflag
{
	struct MontageEvent
	{
		uint64_t Sequence = 0;
		std::string Slot, Section;
		AnimationNotice Notice;
	};

	struct MontageTarget { glm::vec3 Position; float Yaw; };

	struct MontageLayerSample
	{
		std::string Slot, Clip, State, Section;
		float Time, Weight;
		bool Fading;
	};

	/* 一名角色持有一份。先推进播放时间、发送通知，再把各 Slot 的局部姿势叠到走跑姿势上。 */
	class MontagePlayer
	{
	public:
		using WarpStep = std::function<void(const MontageTarget&, const glm::vec3&, const glm::vec3&, float, float)>;
		bool Play(Ref<const AnimationMontage> asset, Ref<ModelAnimation> skeleton, const std::string& section,
			const std::string& state, bool restart, std::string& error);
		bool Jump(const std::string& slot, const std::string& section);
		bool Stop(const std::string& slot);
		bool IsPlaying(const std::string& slot) const;
		bool IsOpen(const std::string& slot, const std::string& window) const;
		bool HasPose() const { return !m_Layers.empty(); }
		uint64_t LastEvent() const { return m_NextEvent - 1; }
		bool Read(uint64_t& cursor, MontageEvent& result, const std::string& slot = {}) const;
		std::vector<MontageLayerSample> Samples() const;
		void SetTarget(const std::string& name, const MontageTarget& target) { m_Targets[name] = target; }
		bool ClearTarget(const std::string& name) { return m_Targets.erase(name) != 0; }
		void Advance(float seconds, const WarpStep& move = {});
		void Apply(const ModelAnimation& skeleton, std::vector<ModelAnimation::NodePose>& pose) const;
		uint64_t ScriptCursor = 0;
		std::map<std::string, Ref<const AnimationMontage>> Files;

	private:
		struct Layer
		{
			Ref<const AnimationMontage> Asset;
			Ref<ModelAnimation> Skeleton;
			const MontageSection* Section = nullptr;
			std::string State;
			size_t Clip = 0;
			std::vector<size_t> Mask;
			std::vector<AnimationNotice> Marks;
			std::set<std::string> Open;
			std::map<std::string,glm::vec3> Extracted;
			float Time = 0, Age = 0, Weight = 0, FadeTime = 0, FadeFrom = 0;
			bool Fading = false;
		};
		Layer* Active(const std::string& slot);
		void Emit(Layer& layer, AnimationNotice notice);
		void CloseWindows(Layer& layer);
		void Enter(Layer& layer, const MontageSection& section);
		void Fade(Layer& layer);
		void Warp(Layer& layer, float from, float to, const WarpStep& move) const;
		std::vector<Layer> m_Layers;
		std::map<std::string, MontageTarget> m_Targets;
		std::deque<MontageEvent> m_Events;
		uint64_t m_NextEvent = 1;
	};
}
