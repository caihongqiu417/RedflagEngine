#pragma once
#include "Redflag/AI/SpeechInput.h"
#include "Redflag/AI/SpeechPlayback.h"
#include <functional>

namespace Redflag
{
	class Scene;
	class VoicePanel
	{
	public:
		VoicePanel();
		~VoicePanel();
		void Draw(Scene& scene, uint64_t actor, const std::function<void(const std::string&)>& submit);
		void Reset();
		void Speak(uint64_t speaker, const std::string& text, const std::string& voice);
		void StopPlayback() { m_Playback.Stop(); }
		void UpdatePlayback() { m_Playback.Update(); }

	private:
		struct Microphone;
		std::unique_ptr<Microphone> m_Microphone;
		SpeechInput m_Input;
		SpeechPlayback m_Playback;
		Scene* m_Scene = nullptr;
		uint64_t m_Actor = 0;
		bool m_RoomOpen = false;
		std::string m_Message;
	};
}
