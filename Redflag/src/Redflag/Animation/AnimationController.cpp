#include "rfpch.h"
#include "AnimationController.h"
#include "Montage.h"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <set>

namespace Redflag
{
	std::filesystem::path AnimationState::MontageFile(const std::filesystem::path& controller) const
	{
		const std::filesystem::path source = std::filesystem::u8path(Montage);
		if (source.empty() || source.is_absolute() || *source.begin() == "assets") return source;
		return controller.parent_path() / source;
	}

	float AnimationTransition::Weight(float t) const
	{
		t = std::clamp(t, 0.f, 1.f);
		const float s = 1.f - t;
		// 两个控制点在时间轴的三分之一、三分之二处。默认纵坐标 0、1 让两端速度降为零。
		return 3.f * s * s * t * Control.x + 3.f * s * t * t * Control.y + t * t * t;
	}

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
				state.Clip = row["Clip"].as<std::string>("");
				state.Montage = row["Montage"].as<std::string>("");
				state.Loop = row["Loop"].as<bool>(true);
				state.Blend = row["Blend"].as<float>(state.Blend);
				state.Contact = row["Contact"].as<float>(state.Contact);
				state.HasContact = row["Contact"].IsDefined();
				state.Seconds = row["Seconds"].as<float>(state.Seconds);

				if (!state.Montage.empty())
				{
					if (row["Events"] || row["Contact"]) throw std::runtime_error(state.ID + " 的通知应写入蒙太奇文件。");
					const std::filesystem::path source = state.MontageFile(path);
					auto montage = std::make_shared<AnimationMontage>();
					if (!montage->Load(source,error)) throw std::runtime_error(source.u8string() + " " + error);
					if (!state.Clip.empty() && state.Clip != montage->Clip) throw std::runtime_error(state.ID + " 的 Clip 和蒙太奇原片不一致。");
					state.Clip = montage->Clip;
					state.MontageAsset = montage;

					// 手部 IK 需要知道还要多久才能碰到物品。实际交付由播放时经过的 contact 通知触发。
					state.Seconds = montage->Duration();
					const std::optional<float> contact = montage->EventTime("contact");
					state.HasContact = contact.has_value();
					if (contact) state.Contact = *contact / state.Seconds;
				}

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
			std::vector<AnimationTransition> transitions;
			std::set<std::pair<std::string, std::string>> pairs;
			const YAML::Node edges = data["Transitions"];
			if (edges && !edges.IsSequence()) throw std::runtime_error("Transitions 应为状态过渡列表");
			for (const YAML::Node& row : edges)
			{
				AnimationTransition edge;
				edge.From = row["From"].as<std::string>();
				edge.To = row["To"].as<std::string>();
				edge.Seconds = row["Seconds"].as<float>();
				if (const YAML::Node control = row["Control"])
				{
					if (!control.IsSequence() || control.size() != 2) throw std::runtime_error("Control 需要两个控制点纵坐标");
					edge.Control = {control[0].as<float>(), control[1].as<float>()};
				}
				if (!ids.count(edge.From) || !ids.count(edge.To) || !pairs.emplace(edge.From, edge.To).second ||
					!std::isfinite(edge.Seconds) || edge.Seconds < 0 ||
					!(edge.Control.x >= 0 && edge.Control.x <= 1 && edge.Control.y >= 0 && edge.Control.y <= 1))
					throw std::runtime_error("过渡的状态、时长或控制点无效 " + edge.From + " → " + edge.To);
				transitions.push_back(std::move(edge));
			}
			m_Transitions = std::move(transitions);
			m_States = std::move(states);
			error.clear();
			return true;
		}
		catch (const std::exception& e) { error = e.what(); return false; }
	}

	const AnimationState* AnimationController::FindClip(const std::string& clip) const
	{
		for (const AnimationState& state : m_States)
			if (state.Clip == clip) return &state;
		return nullptr;
	}

	const AnimationTransition* AnimationController::FindTransition(const std::string& from, const std::string& to) const
	{
		for (const AnimationTransition& edge : m_Transitions)
			if (edge.From == from && edge.To == to) return &edge;
		return nullptr;
	}

	const AnimationState* AnimationController::Find(const std::string& id) const
	{
		for (const AnimationState& state : m_States)
			if (state.ID == id) return &state;
		return nullptr;
	}
}
