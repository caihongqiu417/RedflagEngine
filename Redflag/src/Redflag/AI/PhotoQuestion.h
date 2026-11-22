#pragma once

#include "NpcPhoto.h"
#include "Redflag/Core/Core.h"
#include <chrono>

namespace Redflag
{
	class Scene;

	enum class PhotoSubject { Object, Lighting, Description };

	/* 保留拍摄时的照片和模型的判断。特写里的实体编号只用于追溯当时的画面。 */
	class PhotoQuestion
	{
	public:
		explicit PhotoQuestion(uint16_t port = 8716) : m_Client(port) {}
		bool Start(const Ref<Scene>& scene, NpcPhoto photo, PhotoSubject subject, const std::string& question, bool suggestPlace = true);
		bool Update();
		bool Pending() const { return m_Request != 0; }
		const NpcPhoto& GetPhoto() const { return m_Photo; }
		const std::string& Answer() const { return m_Answer; }
		const std::string& Receipt() const { return m_Receipt; }
		uint64_t Target() const { return m_Target; }
		void Clear();

	private:
		DecisionClient m_Client;
		std::weak_ptr<Scene> m_Scene;
		std::chrono::steady_clock::time_point m_Session;
		NpcPhoto m_Photo;
		PhotoSubject m_Subject{};
		uint64_t m_Request = 0, m_Target = 0;
		std::string m_SearchText, m_Answer, m_Receipt;
		bool m_Verifying = false;
		bool m_SuggestPlace = true;
	};
}
