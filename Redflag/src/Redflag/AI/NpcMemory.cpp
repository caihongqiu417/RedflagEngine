#include "rfpch.h"
#include "NpcMemory.h"
#include <iomanip>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
	// ---- 现场观察与玩家转述 ----
	void NpcMemory::Configure(const MemoryPolicy& policy)
	{
		if (m_Policy == policy) return;
		m_Policy = policy;
		auto forbidden = [&](const NpcEvidence& e) {
			if (e.Source == EvidenceSource::Photo) return !policy.Pictures;
			if (e.Source == EvidenceSource::Reported) return !policy.Reports;
			if (e.Source == EvidenceSource::LastSeen && !policy.KeepSight) return true;
			return !policy.Sight;
		};
		m_Evidence.erase(std::remove_if(m_Evidence.begin(), m_Evidence.end(), forbidden), m_Evidence.end());
		if (m_Evidence.size() > policy.Limit) m_Evidence.erase(m_Evidence.begin(), m_Evidence.end() - policy.Limit);
		for (auto it = m_Preferences.begin(); it != m_Preferences.end();)
			if (!policy.Preferences.count(it->first))
				it = m_Preferences.erase(it);
			else
				++it;
	}

	void NpcMemory::BeginSight()
	{
		for (NpcEvidence& e : m_Evidence)
			if (e.Source == EvidenceSource::Seen) e.Source = EvidenceSource::LastSeen;
		if (!m_Policy.KeepSight)
		{
			auto previous = [](const NpcEvidence& e) { return e.Source == EvidenceSource::LastSeen; };
			m_Evidence.erase(std::remove_if(m_Evidence.begin(), m_Evidence.end(), previous), m_Evidence.end());
		}
	}

	void NpcMemory::LoseSight(uint64_t target)
	{
		auto entry = std::find_if(m_Evidence.begin(), m_Evidence.end(), [target](const NpcEvidence& note) {
			return note.Target == target && note.Source == EvidenceSource::Seen;
		});
		if (entry == m_Evidence.end()) return;
		if (m_Policy.KeepSight) entry->Source = EvidenceSource::LastSeen;
		else m_Evidence.erase(entry);
	}

	bool NpcMemory::See(uint64_t target, const std::string& text, double seconds, const std::string& image)
	{
		return m_Policy.Sight && Write({EvidenceSource::Seen, target, text, image, seconds});
	}

	bool NpcMemory::Report(uint64_t target, const std::string& text, double seconds, const std::string& image,
		std::optional<glm::vec3> place)
	{
		return m_Policy.Reports && Write({EvidenceSource::Reported, target, text, image, seconds, place});
	}

	bool NpcMemory::Write(NpcEvidence entry)
	{
		if (entry.Text.empty() || !m_Policy.Limit) return false;
		auto found = std::find_if(m_Evidence.begin(), m_Evidence.end(), [&](const NpcEvidence& old) {
			if (entry.Source == EvidenceSource::Photo || old.Source == EvidenceSource::Photo)
				return entry.Source == old.Source && old.Image == entry.Image;
			bool sameSource = (old.Source == EvidenceSource::Reported) == (entry.Source == EvidenceSource::Reported);
			if (!entry.Target && entry.Image.empty()) return false;
			return sameSource && old.Target == entry.Target && (entry.Target || old.Image == entry.Image);
		});
		if (found != m_Evidence.end()) m_Evidence.erase(found);
		if (m_Evidence.size() == m_Policy.Limit) m_Evidence.erase(m_Evidence.begin());
		entry.Number = m_NextNumber++;
		m_Evidence.push_back(std::move(entry));
		return true;
	}

	bool NpcMemory::RememberPhoto(uint64_t target, const std::string& text, double capturedAt, const std::string& image,
		std::optional<glm::vec3> place)
	{
		return m_Policy.Pictures && !image.empty() && Write({EvidenceSource::Photo, target, text, image, capturedAt, place});
	}

	void NpcMemory::MarkVisited(uint64_t number)
	{
		for (NpcEvidence& entry : m_Evidence)
			if (entry.Number == number) { entry.Visited = true; return; }
	}

	// ---- 偏好与玩家存档 ----
	bool NpcMemory::SetPreference(const std::string& key, const std::string& value)
	{
		if (!m_Policy.Preferences.count(key)) return false;
		if (value.empty())
			m_Preferences.erase(key);
		else
			m_Preferences[key] = value;
		return true;
	}

	const std::string* NpcMemory::GetPreference(const std::string& key) const
	{
		const auto it = m_Preferences.find(key);
		return it == m_Preferences.end() ? nullptr : &it->second;
	}

	std::string NpcMemory::SavePreferences() const
	{
		YAML::Node result(YAML::NodeType::Map);
		for (const auto& [key, value] : m_Preferences)
			if (m_Policy.Preferences.at(key) == PreferenceScope::Saved) result[key] = value;
		return YAML::Dump(result);
	}

	bool NpcMemory::LoadPreferences(const std::string& save)
	{
		try
		{
			YAML::Node file = YAML::Load(save);
			if (!file.IsMap()) return false;
			// 先读完整份存档。遇到损坏的条目时，保留玩家原来的偏好。
			auto next = m_Preferences;
			for (const auto& [key, scope] : m_Policy.Preferences)
			{
				if (scope != PreferenceScope::Saved) continue;
				next.erase(key);
				if (file[key])
				{
					if (!file[key].IsScalar()) return false;
					next[key] = file[key].as<std::string>();
				}
			}
			m_Preferences.swap(next);
			return true;
		}
		catch (const YAML::Exception&)
		{
			return false;
		}
	}

	// ---- 模型收到的上下文 ----
	void NpcMemory::AddTo(DecisionRequest& request) const
	{
		for (const NpcEvidence& e : m_Evidence)
		{
			const char* source = "reported";
			const char* title = "玩家转述，尚未核实";
			if (e.Source == EvidenceSource::Photo)
			{
				source = "photo";
				title = "对当时照片的判断，可能认错";
			}
			else if (e.Source == EvidenceSource::Seen)
			{
				source = "seen";
				title = "现场看见";
			}
			else if (e.Source == EvidenceSource::LastSeen)
			{
				source = "last_seen";
				title = "上次看见，当前未确认";
			}
			std::string id = "note/" + std::to_string(e.Number);
			if (!e.Image.empty()) id = "image/" + e.Image;
			if (e.Target && e.Source != EvidenceSource::Photo) id = "entity/" + std::to_string(e.Target);
			std::ostringstream text;
			text << title << " | " << std::fixed << std::setprecision(2) << e.Seconds << " s | " << e.Text;
			if (!e.Image.empty()) text << " | 图片 " << e.Image;
			if (e.Visited) text << " | 已尝试前往查看";
			request.Observations.push_back({std::string(source) + "/" + id, text.str()});
		}
		for (const auto& [key, value] : m_Preferences)
			request.Observations.push_back({"preference/" + key, value});
	}
} // namespace Redflag
