#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Redflag
{
	struct AnimationMarker
	{
		std::string Name;
		float Start = 0.f;
		std::optional<float> End; // 不设置结束时间就只在 Start 通知一次。两项均为剪辑内的秒数。
		bool operator==(const AnimationMarker& other) const { return Name==other.Name && Start==other.Start && End==other.End; }
	};

	struct AnimationState
	{
		std::string ID, Clip;
		bool Loop = true;
		float Blend = .18f, Contact = .5f, Seconds = 0.0f;
		bool HasContact = false;
		std::vector<AnimationMarker> Events;
	};

	/* 角色共用这张动作表。当前动作、已经播放的时间都由场景里的角色自己保存。 */
	class AnimationController
	{
	public:
		/// <summary>读取动作与模型剪辑的对应关系，同时检查重复编号、过渡时间和接触时刻。</summary>
		bool Load(const std::filesystem::path& path, std::string& error);

		const AnimationState* Find(const std::string& id) const;
		const std::vector<AnimationState>& GetStates() const { return m_States; }

	private:
		std::vector<AnimationState> m_States;
	};
}
