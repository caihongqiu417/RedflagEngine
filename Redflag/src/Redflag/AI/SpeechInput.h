#pragma once
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Redflag
{
	enum class SpeechEventType { RoomOpened, Transcript, Error };
	struct SpeechEvent { SpeechEventType Type; std::string Text; };

	/* 玩家对当前角色说的话。游戏线程在 Take 中取走识别结果，切换角色时调用 Reset。 */
	class SpeechInput
	{
	public:
		explicit SpeechInput(uint16_t port = 0);
		~SpeechInput();
		SpeechInput(const SpeechInput&) = delete;
		SpeechInput& operator=(const SpeechInput&) = delete;
		void OpenRoom();
		void SubmitWav(std::string bytes);
		std::optional<SpeechEvent> Take();
		void Reset();

	private:
		struct Session;
		std::shared_ptr<Session> m_Session;
		std::vector<std::future<void>> m_Work;
		uint16_t m_Port;
	};
}
