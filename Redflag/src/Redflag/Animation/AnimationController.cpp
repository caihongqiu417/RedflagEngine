#include "rfpch.h"
#include "AnimationController.h"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <set>

namespace Redflag
{
	bool AnimationController::Load(const std::filesystem::path& path, std::string& error)
	{
		std::ifstream file(path);
		if (!file) { error = "无法打开动作表 " + path.u8string(); return false; }
		try
		{
			std::vector<AnimationState> states;
			std::set<std::string> ids;
			const YAML::Node data = YAML::Load(file);
			if (!data["States"].IsSequence()) throw std::runtime_error("动作表缺少 States");

			for (const YAML::Node& row : data["States"])
			{
				AnimationState state;
				state.ID = row["ID"].as<std::string>();
				state.Clip = row["Clip"].as<std::string>();
				state.Loop = row["Loop"].as<bool>(true);
				state.Blend = row["Blend"].as<float>(state.Blend);
				state.Contact = row["Contact"].as<float>(state.Contact);
				state.HasContact = row["Contact"].IsDefined();
				state.Seconds = row["Seconds"].as<float>(state.Seconds);

				if (state.ID.empty() || state.Clip.empty() || !ids.insert(state.ID).second ||
					!(state.Blend >= 0 && state.Seconds >= 0 && state.Contact >= 0 && state.Contact <= 1))
					throw std::runtime_error("动作编号重复、剪辑为空或时间无效 " + state.ID);
				std::set<std::string> names;
				if (state.HasContact || state.ID == "pickup" || state.ID == "give")
					names.insert("contact"); // 拿取与递物沿用 Contact 指定的接触时刻。
				const YAML::Node events = row["Events"];
				if (events && !events.IsSequence()) throw std::runtime_error(state.ID + " 的 Events 应为事件列表");
				for (const YAML::Node& event : events) {
					AnimationMarker marker;
					marker.Name = event["Name"].as<std::string>();
					marker.Start = event["Start"].as<float>();
					if (event["End"]) marker.End = event["End"].as<float>();
					if (marker.Name.empty() || !names.insert(marker.Name).second || !std::isfinite(marker.Start) ||
						marker.Start < 0 || (marker.End && (!std::isfinite(*marker.End) || *marker.End <= marker.Start)) ||
						(state.Seconds > 0 && marker.End.value_or(marker.Start) > state.Seconds))
						throw std::runtime_error(state.ID + " 事件名称重复或时间无效 " + marker.Name);
					state.Events.push_back(std::move(marker));
				}
				states.push_back(std::move(state));
			}
			m_States = std::move(states);
			error.clear();
			return true;
		}
		catch (const std::exception& e) { error = e.what(); return false; }
	}

	const AnimationState* AnimationController::Find(const std::string& id) const
	{
		for (const AnimationState& state : m_States)
			if (state.ID == id) return &state;
		return nullptr;
	}
}
