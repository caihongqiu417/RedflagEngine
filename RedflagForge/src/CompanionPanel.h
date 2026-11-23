#pragma once

#include "BehaviorTreePanel.h"
#include "NpcAuthoringPanel.h"
#include "VoicePanel.h"
#include "UI/GameConversation.h"
#include "Redflag/AI/NpcDefinition.h"
#include <functional>

namespace Redflag
{
	class Scene;

	/* Forge 编辑独立的角色资产或检查选中的实体。游戏对话使用 HUD 指定的角色。 */
	class CompanionPanel
	{
	public:
		void OpenAssets();
		void SetMontageEditor(std::function<void(const std::filesystem::path&)> open) { m_Author.OpenMontage = std::move(open); }
		void DrawInspector(Entity actor, bool playing);
		void Draw(Scene& scene, bool playing, const std::function<void(uint64_t)>& drawPhoto = {});
		void Update(Scene& scene);
		void DrawGame(Scene& scene, const ConversationStyle& style, ImVec2 corner, ImVec2 size);
		void Stop();

	private:
		uint64_t m_Actor = 0, m_Speaker = 0, m_AuthorActor = 0;
		bool m_Open = false, m_ShowTree = false, m_Authoring = false;
		uint64_t m_SpeechCursor = 0, m_Request = 0;
		GameConversation m_Chat;
		std::string m_Asset;
		BehaviorTreePanel m_TreePanel;
		NpcAuthoringPanel m_Author;
		VoicePanel m_Voice;
	};
}
