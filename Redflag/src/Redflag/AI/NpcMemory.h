#pragma once

#include "DecisionClient.h"
#include <glm/vec3.hpp>
#include <map>
#include <optional>

namespace Redflag
{
	enum class EvidenceSource
	{
		Seen,
		LastSeen,
		Reported,
		Photo
	};

	enum class PreferenceScope
	{
		Session,
		Saved
	};

	struct MemoryPolicy
	{
		bool Sight = true, KeepSight = true, Reports = true, Pictures = true;
		size_t Limit = 32; // 每个角色保留最近这么多条观察或转述，调大会增加模型收到的文字。
		std::map<std::string, PreferenceScope> Preferences;

		bool operator==(const MemoryPolicy& other) const
		{
			return Sight == other.Sight && KeepSight == other.KeepSight && Reports == other.Reports && Pictures == other.Pictures && Limit == other.Limit &&
			       Preferences == other.Preferences;
		}
	};

	struct NpcEvidence
	{
		EvidenceSource Source;
		uint64_t Target;
		std::string Text, Image;
		double Seconds; // 从本局开始到观察或转述发生时经过的秒数。
		std::optional<glm::vec3> Place; // 可以去查看的位置，未必是物体的位置。
		uint64_t Number = 0;
		bool Visited = false;
	};

	/* 宿主提供亲眼所见的物体和玩家的原话。两种来源各自保留，相同物体可以同时有两条记录。 */
	class NpcMemory
	{
	public:
		void Configure(const MemoryPolicy& policy);

		// 一次观察从 BeginSight 开始。上次见过、这次没有再次看到的物体保留上次的记录时间。
		void BeginSight();
		void LoseSight(uint64_t target);
		bool See(uint64_t target, const std::string& text, double seconds, const std::string& image = {});
		bool Report(uint64_t target, const std::string& text, double seconds, const std::string& image = {},
			std::optional<glm::vec3> place = {});
		bool RememberPhoto(uint64_t target, const std::string& text, double capturedAt, const std::string& image,
			std::optional<glm::vec3> place = {});
		void MarkVisited(uint64_t number);
		const std::vector<NpcEvidence>& GetEvidence() const { return m_Evidence; }

		// ---- 偏好与玩家存档 ----
		bool SetPreference(const std::string& key, const std::string& value);
		const std::string* GetPreference(const std::string& key) const;
		std::string SavePreferences() const;
		bool LoadPreferences(const std::string& save);
		void AddTo(DecisionRequest& request) const;

	private:
		bool Write(NpcEvidence entry);
		MemoryPolicy m_Policy;
		std::vector<NpcEvidence> m_Evidence;
		uint64_t m_NextNumber = 1;
		std::map<std::string, std::string> m_Preferences;
	};
} // namespace Redflag
