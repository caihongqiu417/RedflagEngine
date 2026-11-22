#pragma once

#include "BehaviorTreePanel.h"
#include "NpcAuthoringPanel.h"
#include "VoicePanel.h"
#include "Redflag/AI/NpcDefinition.h"
#include <functional>

namespace Redflag
{
	class Scene;

	/* 编辑时查看角色定义，播放时把玩家的话发给当前选中的队友。 */
	class CompanionPanel
	{
	public:
		void DrawMenu();
		void Draw(Scene& scene, bool playing, const std::function<void(uint64_t)>& drawPhoto = {});
		void Stop();

	private:
		uint64_t m_Actor = 0;
		bool m_Open = false, m_ShowTree = false, m_Authoring = false;
		uint64_t m_SpeechCursor = 0;
		char m_Text[1024] = {};
		std::string m_Path, m_Error;
		NpcDefinition m_Editing;
		BehaviorTreePanel m_TreePanel;
		NpcAuthoringPanel m_Author;
		VoicePanel m_Voice;
	};
}
