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

	private:
		DecisionClient m_Client;
		std::weak_ptr<Scene> m_Scene;
		uint64_t m_Pending = 0;
		std::string m_Message;
		char m_Prompt[512] = "";
		char m_Model[128] = "";
		bool m_PickObject = false;
	};
}
