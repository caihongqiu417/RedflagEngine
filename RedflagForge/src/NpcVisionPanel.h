#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Scene/NpcVision.h"
#include <filesystem>

namespace Redflag
{
	class NpcVisionPanel
	{
	public:
		void Draw(const Ref<Scene>& scene, const std::filesystem::path& file, bool simulating, glm::vec3 eye);
	private:
		std::future<std::optional<uint64_t>> m_Answer;
		std::weak_ptr<Scene> m_QuestionScene;
		char m_Question[256] = "找一下红箱子";
		float m_Reach = 1.5f;
		std::string m_Message;
	};
}
