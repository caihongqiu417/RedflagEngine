#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Scene/NpcVision.h"
#include <filesystem>

namespace Redflag
{
	class EditorCamera;

	class NpcVisionPanel
	{
	public:
		// 用户按下“从拍摄机位看向该处”时返回 true，编辑器随后显示场景相机。
		bool Draw(const Ref<Scene>& scene, const std::filesystem::path& file, bool simulating, EditorCamera& camera);
	private:
		std::future<NpcVisionCandidate> m_Answer;
		std::optional<NpcViewRegion> m_Region;
		std::weak_ptr<Scene> m_QuestionScene;
		char m_Question[256] = "找一下红箱子";
		float m_Reach = 1.5f;
		std::string m_Message;
	};
}
