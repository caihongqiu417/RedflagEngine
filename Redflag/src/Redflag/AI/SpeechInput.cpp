#include "rfpch.h"
#include "SpeechInput.h"
#include "LocalService.h"
#include <condition_variable>
#include <deque>

namespace Redflag
{
	struct SpeechInput::Session
	{
		std::mutex Mutex;
		std::condition_variable Wake;
		std::deque<SpeechEvent> Events;
		bool Closed = false;
		uint64_t LastRecording = 0;

		void Emit(SpeechEventType type, std::string text, uint64_t recording = 0)
		{
			std::lock_guard<std::mutex> lock(Mutex);
			if (!Closed && (!recording || recording == LastRecording))
				Events.push_back({type, std::move(text)});
		}
	};

	SpeechInput::SpeechInput(uint16_t port) : m_Session(std::make_shared<Session>()), m_Port(port) {}

	SpeechInput::~SpeechInput() { Reset(); }

	void SpeechInput::OpenRoom()
	{
		m_Work.push_back(std::async(std::launch::async, [session=m_Session, port=m_Port] {
			std::unique_ptr<LocalService> service;
			std::wstring path;
			try {
				service = std::make_unique<LocalService>(port);
				std::string room = service->Send(L"POST", L"/v1/speech/rooms", "{}")["room"].as<std::string>();
				if (room.size() != 32 || room.find_first_not_of("0123456789abcdef") != std::string::npos)
					throw std::runtime_error("invalid_voice_room");
				path = L"/v1/speech/rooms/" + std::wstring(room.begin(),room.end());
				session->Emit(SpeechEventType::RoomOpened,"http://127.0.0.1:"+std::to_string(port)+"/room/"+room);

				for (;;) {
					std::unique_lock<std::mutex> lock(session->Mutex);
					if (session->Wake.wait_for(lock,std::chrono::milliseconds(200),[&]{ return session->Closed; })) break;
					lock.unlock();
					YAML::Node response = service->Send(L"GET",path);
					for (const YAML::Node& text : response["texts"]) session->Emit(SpeechEventType::Transcript,text.as<std::string>());
				}
			} catch (const std::exception& error) { session->Emit(SpeechEventType::Error,error.what()); }
			if (!path.empty()) {
				try { service->Send(L"DELETE",path); }
				catch (const std::exception&) { /* 服务已关闭时，这个房间也已失效。 */ }
			}
		}));
	}

	void SpeechInput::SubmitWav(std::string bytes)
	{
		uint64_t recording;
		{
			std::lock_guard<std::mutex> lock(m_Session->Mutex);
			recording = ++m_Session->LastRecording;
		}
		m_Work.push_back(std::async(std::launch::async, [session=m_Session, port=m_Port, recording, data=std::move(bytes)] {
			try {
				LocalService service(port,15000);
				std::string text = service.Send(L"POST",L"/v1/speech/transcribe",data,L"Content-Type: audio/wav")["text"].as<std::string>();
				session->Emit(SpeechEventType::Transcript, std::move(text), recording);
			} catch (const std::exception& error) { session->Emit(SpeechEventType::Error, error.what(), recording); }
		}));
	}

	std::optional<SpeechEvent> SpeechInput::Take()
	{
		m_Work.erase(std::remove_if(m_Work.begin(),m_Work.end(),[](std::future<void>& work) {
			return work.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
		}),m_Work.end());
		std::lock_guard<std::mutex> lock(m_Session->Mutex);
		if (m_Session->Events.empty()) return {};
		SpeechEvent event = std::move(m_Session->Events.front());
		m_Session->Events.pop_front();
		return event;
	}

	void SpeechInput::Reset()
	{
		// 工作线程还持有旧会话。关掉它后，晚回来的文字不会进入另一个角色的对话。
		{
			std::lock_guard<std::mutex> lock(m_Session->Mutex);
			m_Session->Closed = true;
			m_Session->Wake.notify_all();
		}
		m_Session = std::make_shared<Session>();
	}
}
