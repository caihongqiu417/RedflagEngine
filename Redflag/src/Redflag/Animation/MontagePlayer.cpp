#include "rfpch.h"
#include "MontagePlayer.h"

namespace Redflag
{
	MontagePlayer::Layer* MontagePlayer::Active(const std::string& slot)
	{
		for (Layer& layer : m_Layers) if (!layer.Fading && layer.Asset->Slot == slot) return &layer;
		return nullptr;
	}

	bool MontagePlayer::Play(Ref<const AnimationMontage> asset, Ref<ModelAnimation> skeleton, const std::string& section,
		const std::string& state, bool restart, std::string& error)
	{
		if (!asset || !skeleton) { error = "缺少蒙太奇或带骨骼的模型。"; return false; }
		if (!asset->Validate(error)) return false;
		const MontageSection* start = section.empty() ? &asset->Sections.front() : asset->Find(section);
		if (!start) { error = "没有这个播放段：" + section; return false; }
		Layer* previous = Active(asset->Slot);
		const std::string& action = state.empty() ? asset->Name : state;
		if (!restart && previous && previous->Asset == asset && previous->Skeleton == skeleton && previous->State == action) return true;

		Layer layer;
		layer.Asset = asset;
		layer.Skeleton = skeleton;
		layer.State = action;
		const auto& clips = skeleton->GetClips();
		const auto clip = std::find_if(clips.begin(), clips.end(), [&](const ModelAnimation::Clip& c) { return c.Name == asset->Clip; });
		if (clip == clips.end()) { error = "模型中没有剪辑：" + asset->Clip; return false; }
		for (const MontageSection& range : asset->Sections)
			if (range.End > clip->Duration + .0001f) { error = "播放段超出了原片时长：" + range.Name; return false; }
		if (!skeleton->SubtreeMask(asset->Bones, layer.Mask)) { error = "模型中没有指定的遮罩根骨。"; return false; }
		for (const MontageWarp& warp : asset->WarpWindows) {
			glm::mat4 root;
			if (!skeleton->GetRestTransform(warp.Root, root)) { error = "模型中没有根骨：" + warp.Root; return false; }
		}
		layer.Clip = clip - clips.begin();
		for (const AnimationMarker& marker : asset->Events) {
			layer.Marks.push_back({layer.State, marker.Name, marker.End ? NotifyPhase::Begin : NotifyPhase::Point, marker.Start});
			if (marker.End) layer.Marks.push_back({layer.State, marker.Name, NotifyPhase::End, *marker.End});
		}
		std::stable_sort(layer.Marks.begin(), layer.Marks.end(), [](const AnimationNotice& a, const AnimationNotice& b) { return a.Seconds < b.Seconds; });
		layer.Weight = asset->BlendIn.Seconds == 0 ? 1.f : 0.f;

		// 新动作与骨架都通过检查后才淡出原 Slot；无效资源不会挤掉正在播放的动作。
		if (previous) Fade(*previous);
		m_Layers.push_back(std::move(layer));
		Enter(m_Layers.back(), *start);
		return true;
	}

	void MontagePlayer::Emit(Layer& layer, AnimationNotice notice)
	{
		if (notice.Phase == NotifyPhase::Begin) layer.Open.insert(notice.Name);
		if (notice.Phase == NotifyPhase::End) layer.Open.erase(notice.Name);
		m_Events.push_back({m_NextEvent++, layer.Asset->Slot, layer.Section->Name, std::move(notice)});
		if (m_Events.size() > 256) m_Events.pop_front(); // 给脚本和调试器各自保留最近 256 条通知。
	}

	void MontagePlayer::CloseWindows(Layer& layer)
	{
		while (!layer.Open.empty()) Emit(layer, {layer.State, *layer.Open.begin(), NotifyPhase::End, layer.Time, true});
	}

	void MontagePlayer::Enter(Layer& layer, const MontageSection& section)
	{
		CloseWindows(layer);
		layer.Section = &section;
		layer.Extracted.clear();
		layer.Time = section.Start;
		for (const AnimationMarker& mark : layer.Asset->Events) {
			if (mark.End && mark.Start < layer.Time && layer.Time < *mark.End)
				Emit(layer, {layer.State, mark.Name, NotifyPhase::Begin, layer.Time});
		}
		for (const AnimationNotice& mark : layer.Marks)
			if (mark.Seconds == layer.Time && mark.Phase != NotifyPhase::End) Emit(layer, mark);
	}

	bool MontagePlayer::Jump(const std::string& slot, const std::string& section)
	{
		Layer* layer = Active(slot);
		const MontageSection* destination = layer ? layer->Asset->Find(section) : nullptr;
		if (!destination) return false;
		Enter(*layer, *destination);
		return true;
	}

	void MontagePlayer::Fade(Layer& layer)
	{
		CloseWindows(layer);
		layer.Fading = true;
		layer.FadeFrom = layer.Weight;
		if (!layer.Asset->BlendOut.Seconds) layer.Weight = 0;
	}

	bool MontagePlayer::Stop(const std::string& slot)
	{
		Layer* layer = Active(slot);
		if (!layer) return false;
		Fade(*layer);
		return true;
	}

	bool MontagePlayer::IsPlaying(const std::string& slot) const
	{
		for (const Layer& layer : m_Layers) if (!layer.Fading && layer.Asset->Slot == slot) return true;
		return false;
	}

	bool MontagePlayer::IsOpen(const std::string& slot, const std::string& window) const
	{
		for (const Layer& layer : m_Layers)
			if (!layer.Fading && layer.Asset->Slot == slot) return layer.Open.count(window) != 0;
		return false;
	}

	bool MontagePlayer::Read(uint64_t& cursor, MontageEvent& result, const std::string& slot) const
	{
		for (const MontageEvent& event : m_Events) {
			if (event.Sequence <= cursor) continue;
			cursor = event.Sequence;
			if (slot.empty() || event.Slot == slot) { result = event; return true; }
		}
		return false;
	}

	//========== 根骨移动与姿势叠加 ==========

	std::vector<MontageLayerSample> MontagePlayer::Samples() const
	{
		std::vector<MontageLayerSample> result;
		result.reserve(m_Layers.size());
		for (const Layer& layer : m_Layers)
			result.push_back({layer.Asset->Slot, layer.Asset->Clip, layer.State, layer.Section->Name, layer.Time, layer.Weight, layer.Fading});
		return result;
	}

	void MontagePlayer::Warp(Layer& layer, float from, float to, const WarpStep& move) const
	{
		for (const MontageWarp& warp : layer.Asset->WarpWindows) {
			const float first = std::max(from, warp.Start), last = std::min(to, warp.End);
			const auto target = m_Targets.find(warp.Target);
			if (first >= last || target == m_Targets.end()) continue;
			auto position = [&](float time) {
				std::vector<ModelAnimation::NodePose> pose;
				std::vector<glm::mat4> transforms;
				layer.Skeleton->SamplePose(layer.Clip, time, pose);
				layer.Skeleton->GetModelTransforms(pose, transforms);
				glm::mat4 transform(1);
				layer.Skeleton->GetNodeTransform(warp.Root, transforms, transform);
				return glm::vec3(transform[3]);
			};
			const glm::vec3 start = position(first);
			const glm::vec3 delta = position(last) - start;
			move(target->second, delta, position(warp.End) - start, (last - first) / (warp.End - first), last - first);
			// 实体已经沿地面走出的原片位移要从骨架扣除，原片中的上下起伏仍然保留。
			layer.Extracted.try_emplace(warp.Root,0.f).first->second += glm::vec3(delta.x,0.f,delta.z);
		}
	}

	void MontagePlayer::Advance(float seconds, const WarpStep& move)
	{
		if (seconds <= 0) return;
		for (Layer& layer : m_Layers) {
			float remaining = seconds;
			while (!layer.Fading && remaining > 0) {
				const float start = layer.Time;
				const float distance = layer.Section->End - start;
				const float step = std::min(remaining, distance);
				layer.Time = remaining >= distance ? layer.Section->End : start + step;
				remaining -= step;
				layer.Age += step;
				const MontageBlend& blend = layer.Asset->BlendIn;
				layer.Weight = blend.Seconds > 0 ? blend.Weight(layer.Age / blend.Seconds) : 1.f;
				if (move) Warp(layer, start, layer.Time, move);
				for (const AnimationNotice& mark : layer.Marks)
					if (mark.Seconds > start && mark.Seconds <= layer.Time) Emit(layer, mark);
				if (layer.Time < layer.Section->End) break;
				const MontageSection* next = layer.Asset->Find(layer.Section->Next);
				if (!next) Fade(layer);
				else if (next->Start == layer.Time) layer.Section = next;
				else Enter(layer, *next);
			}
			if (layer.Fading) {
				layer.FadeTime += remaining;
				const MontageBlend& blend = layer.Asset->BlendOut;
				layer.Weight = blend.Seconds > 0 ? layer.FadeFrom * (1.f - blend.Weight(layer.FadeTime / blend.Seconds)) : 0;
			}
		}
		m_Layers.erase(std::remove_if(m_Layers.begin(),m_Layers.end(), [](const Layer& layer) { return layer.Fading && layer.Weight <= 0.f; }),m_Layers.end());
	}

	void MontagePlayer::Apply(const ModelAnimation& skeleton, std::vector<ModelAnimation::NodePose>& pose) const
	{
		std::vector<ModelAnimation::NodePose> overlay;
		for (const Layer& layer : m_Layers) {
			if (&skeleton != layer.Skeleton.get() || layer.Weight <= 0) continue;
			skeleton.SamplePose(layer.Clip, layer.Time, overlay);
			for (const auto& [root,delta] : layer.Extracted) skeleton.SubtractRootMotion(overlay,root,delta);
			for (size_t index : layer.Mask) {
				ModelAnimation::NodePose& base = pose[index];
				const ModelAnimation::NodePose& sample = overlay[index];
				base.Position = glm::mix(base.Position, sample.Position, layer.Weight);
				base.Scale = glm::mix(base.Scale, sample.Scale, layer.Weight);
				base.Rotation = glm::normalize(glm::slerp(base.Rotation, sample.Rotation, layer.Weight));
			}
		}
	}
}
