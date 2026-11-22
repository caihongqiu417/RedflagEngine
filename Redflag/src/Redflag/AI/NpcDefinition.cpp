#include "rfpch.h"
#include "NpcDefinition.h"
#include <yaml-cpp/yaml.h>

namespace Redflag
{
	bool NpcDefinition::Load(const std::filesystem::path& path, std::string& error)
	{
		try
		{
			std::ifstream input(path);
			if (!input)
			{
				error = "打不开角色设置：" + path.u8string();
				return false;
			}
			const YAML::Node file = YAML::Load(input);
			NpcDefinition data;
			data.Name = file["Name"].as<std::string>();
			data.Instructions = file["Instructions"].as<std::string>();
			data.LeaderRelationship = file["LeaderRelationship"].as<std::string>("");
			data.IdleIntent = file["IdleIntent"].as<std::string>("");
			data.RescueIntent = file["RescueIntent"].as<std::string>("");
			data.MissingTargetIntent = file["MissingTargetIntent"].as<std::string>("");
			data.Model = file["Model"].as<std::string>("");
			data.Voice = file["Voice"].as<std::string>("");
			data.IdleClip = file["IdleClip"].as<std::string>("");
			data.WalkClip = file["WalkClip"].as<std::string>("");
			data.Sight = file["Sight"].as<float>(data.Sight);
			data.Speed = file["Speed"].as<float>(data.Speed);
			data.Reach = file["Reach"].as<float>(data.Reach);
			data.SearchStep = file["SearchStep"].as<float>(data.SearchStep);
			data.SearchRadius = file["SearchRadius"].as<float>(data.SearchRadius);
			if (!(data.SearchRadius > 0.f && std::isfinite(data.SearchRadius))) { error = "SearchRadius 需要填写正米数"; return false; }
			if (!(data.SearchStep >= 0.f && std::isfinite(data.SearchStep))) { error = "SearchStep 需要填写非负米数"; return false; }
			data.TurnSpeed = file["TurnSpeed"].as<float>(data.TurnSpeed);
			data.ObserveWorld = file["ObserveWorld"].as<bool>(true);
			std::set<std::pair<std::string, std::string>> watched;
			for (const YAML::Node& row : file["Watch"])
			{
				NpcCue cue{row["Team"].as<std::string>(), row["Clip"].as<std::string>(), row["Message"].as<std::string>()};
				if (cue.Team.empty() || cue.Clip.empty() || cue.Message.empty() || !watched.emplace(cue.Team, cue.Clip).second)
				{
					error = "主动观察需要队伍、动作和提醒文字；同队伍的同一动作只写一次";
					return false;
				}
				data.Watch.push_back(std::move(cue));
			}
			data.Remember = file["Remember"].as<bool>(true);
			const YAML::Node memory = file["Memory"];
			if (memory && !memory.IsMap()) { error = "Memory 需要填写角色记忆设置"; return false; }
			data.Memory.Sight = data.ObserveWorld;
			data.Memory.Pictures = file["ObservePictures"].as<bool>(true);
			if (const YAML::Node camera = file["Eyes"])
			{
				data.EyeHeight = camera["Height"].as<float>(data.EyeHeight);
				data.EyeForward = camera["Forward"].as<float>(data.EyeForward);
				data.VerticalFov = camera["VerticalFov"].as<float>(data.VerticalFov);
				if (!(data.VerticalFov > 0.f && data.VerticalFov < 180.f))
				{
					error = "Eyes.VerticalFov 必须大于 0 度、小于 180 度";
					return false;
				}
			}
			data.Memory.KeepSight = data.Remember && (!memory || memory["LastSeen"].as<bool>(true));
			data.Memory.Reports = data.Remember && (!memory || memory["Reports"].as<bool>(true));
			if (memory) {
				data.Memory.Limit=memory["Limit"].as<unsigned>(32);
				const YAML::Node keys=memory["Preferences"];
				if (keys && !keys.IsMap()) { error="Preferences 需要按偏好名填写 session 或 saved"; return false; }
				for (const auto& item : keys) {
					const std::string key=item.first.as<std::string>(), scope=item.second.as<std::string>();
					if (key.empty() || (scope!="session" && scope!="saved")) {
						error="偏好需要名称，保存范围应为 session 或 saved"; return false;
					}
					if (data.Remember) data.Memory.Preferences[key]=scope=="saved" ? PreferenceScope::Saved : PreferenceScope::Session;
				}
			}
			data.TreeFile = path.parent_path() / std::filesystem::u8path(file["Tree"].as<std::string>());
			if (!data.Tree.Load(data.TreeFile, error)) return false;
			if (const YAML::Node animator = file["Animator"])
			{
				data.AnimatorFile = path.parent_path() / std::filesystem::u8path(animator.as<std::string>());
				if (!data.Controller.Load(data.AnimatorFile, error)) return false;
			}
			std::set<std::string> keys;
			for (const YAML::Node& node : file["Intents"])
			{
				NpcIntent intent{node["Key"].as<std::string>(), node["Description"].as<std::string>(),
					node["Target"].as<std::string>(), node["Item"].as<std::string>(""), node["Reply"].as<std::string>("")};
				if (intent.Key.empty() || !keys.insert(intent.Key).second || intent.Target.empty())
				{
					error = "候选需要唯一的动作键和目标来源：" + intent.Key;
					return false;
				}
				data.Intents.push_back(std::move(intent));
			}
			for (const std::string& rule : {data.IdleIntent, data.RescueIntent, data.MissingTargetIntent})
				if (!rule.empty() && !keys.count(rule)) { error = "队伍约定中的动作没有登记：" + rule; return false; }
			*this = std::move(data);
			return true;
		}
		catch (const YAML::Exception& e) { error = e.what(); return false; }
	}
}
