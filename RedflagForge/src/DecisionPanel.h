#pragma once
#include "Redflag/Core/Core.h"
#include "Redflag/AI/DecisionClient.h"

namespace Redflag
{
	class Scene;

	class DecisionPanel
	{
	public:
		void Draw(const Ref<Scene>& scene);
		void DrawSettingsMenu();
		void DrawSettingsWindow();

	private:
		DecisionClient m_Client;
		std::weak_ptr<Scene> m_Scene;
		uint64_t m_Pending = 0;
		std::string m_Message;
		char m_Prompt[512] = "";
		char m_Model[128] = "";
		bool m_PickObject = false;
		bool m_SettingsOpen = false, m_External = false, m_Vision = false;
		char m_Endpoint[1024] = "", m_ServiceModel[128] = "", m_VoiceProject[1024] = "";
		std::string m_ServiceMessage;
	};
}
