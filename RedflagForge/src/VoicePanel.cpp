#include "rfpch.h"
#include "VoicePanel.h"
#include "Redflag/Debug/PlaytestAudio.h"
#include "Redflag/Utils/PlatformUtils.h"
#include <imgui.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <filesystem>

#pragma comment(lib, "winmm.lib")

namespace Redflag
{
	// ---- 默认麦克风 ----
	struct VoicePanel::Microphone
	{
		static constexpr uint32_t SampleRate = 16000;
		static constexpr uint32_t MaxSeconds = 15;
		WAVEFORMATEX Format{ WAVE_FORMAT_PCM, 1, SampleRate, SampleRate * 2, 2, 16, 0 };
		HWAVEIN Device = nullptr;
		WAVEHDR Header{};
		std::vector<char> Samples;
		bool Prepared = false;

		Microphone() : Samples(Format.nAvgBytesPerSec * MaxSeconds)
		{
			// 与中文识别模型相同的单声道格式。一次最多说 15 秒，缓冲区填满时自动提交。
			if (waveInOpen(&Device, WAVE_MAPPER, &Format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
				throw std::runtime_error("无法打开默认麦克风，请检查系统录音权限和输入设备。");

			Header.lpData = Samples.data();
			Header.dwBufferLength = DWORD(Samples.size());
			Prepared = waveInPrepareHeader(Device, &Header, sizeof(Header)) == MMSYSERR_NOERROR;
			if (!Prepared || waveInAddBuffer(Device, &Header, sizeof(Header)) != MMSYSERR_NOERROR || waveInStart(Device) != MMSYSERR_NOERROR)
			{
				Close();
				throw std::runtime_error("麦克风未能开始录音。");
			}
		}

		~Microphone() { Close(); }

		void Close()
		{
			if (!Device) return;
			waveInReset(Device);
			if (Prepared) waveInUnprepareHeader(Device, &Header, sizeof(Header));
			waveInClose(Device);
			Device = nullptr;
		}

		std::string Finish()
		{
			Close();
			const uint32_t bytes = Header.dwBytesRecorded;
			if (!bytes) return {};

			// WAV 先写录音格式，再写样本占用的字节数。PCM 的格式描述占 16 字节。
			std::string wav("RIFF");
			auto appendSize = [&](uint32_t n) { wav.append(reinterpret_cast<const char*>(&n), sizeof(n)); };
			constexpr uint32_t formatBytes = sizeof(PCMWAVEFORMAT);
			appendSize(4 + 8 + formatBytes + 8 + bytes);
			wav += "WAVEfmt ";
			appendSize(formatBytes);
			wav.append(reinterpret_cast<const char*>(&Format), formatBytes);
			wav += "data";
			appendSize(bytes);
			wav.append(Samples.data(), bytes);
			return wav;
		}
	};

	VoicePanel::VoicePanel() = default;
	VoicePanel::~VoicePanel() = default;

	void VoicePanel::Speak(uint64_t speaker, const std::string& text, const std::string& voice)
	{
		m_Playback.Speak(text, voice, ObserveGameSpeech(speaker));
	}

	void VoicePanel::Reset()
	{
		StopPlayback();
		m_Microphone.reset();
		if (m_Scene) m_Input.Reset();
		m_Scene = nullptr;
		m_Actor = 0;
		m_RoomOpen = false;
		m_Message.clear();
	}

	// ---- 录音、文件与浏览器语音房 ----
	void VoicePanel::Draw(Scene& scene, uint64_t actor, const std::function<void(const std::string&)>& submit)
	{
		if (m_Scene != &scene || m_Actor != actor) {
			Reset();
			m_Scene = &scene;
			m_Actor = actor;
		}
		while (std::optional<SpeechEvent> event = m_Input.Take()) {
			if (event->Type == SpeechEventType::RoomOpened) {
				ShellExecuteA(nullptr,"open",event->Text.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
				m_Message = "语音房已打开，浏览器识别的文字会送给当前同伴。";
			} else if (event->Type == SpeechEventType::Error) {
				m_RoomOpen = false;
				m_Message = "语音服务：" + event->Text;
			} else if (!event->Text.empty()) {
				m_Message = "你说：" + event->Text;
				submit(event->Text);
			} else m_Message = "这段录音没有识别到文字。";
		}

		ImGui::Button(m_Microphone ? "录音中，松开发送###NpcVoiceCapture" : "按住说话（V）###NpcVoiceCapture");
		bool press = ImGui::IsItemActivated();
		bool release = ImGui::IsItemDeactivated();
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
			press |= ImGui::IsKeyPressed(ImGuiKey_V,false);
		release |= ImGui::IsKeyReleased(ImGuiKey_V);
		if (press && !m_Microphone) {
			StopPlayback();
			try { m_Microphone = std::make_unique<Microphone>(); }
			catch (const std::exception& error) { m_Message = error.what(); }
		}
		if (m_Microphone && (release || (m_Microphone->Header.dwFlags & WHDR_DONE))) {
			std::string samples = m_Microphone->Finish();
			m_Microphone.reset();
			if (!samples.empty()) {
				m_Input.SubmitWav(std::move(samples));
				m_Message = "正在识别录音…";
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("从 WAV 发送")) {
			std::string file = FileDialogs::OpenFile("WAV audio (*.wav)\0*.wav\0");
			if (!file.empty()) {
				std::ifstream stream(std::filesystem::u8path(file),std::ios::binary|std::ios::ate);
				const std::streamoff size = stream.tellg();
				if (size <= 0 || size > 1000000) m_Message = "请选择不超过 15 秒的 16 kHz 单声道 PCM16 WAV。";
				else {
					std::string bytes(size_t(size),'\0');
					stream.seekg(0);
					if (stream.read(bytes.data(),size)) {
						m_Input.SubmitWav(std::move(bytes));
						m_Message = "正在识别录音…";
					}
				}
			}
		}
		if (ImGui::Button(m_RoomOpen ? "关闭语音房" : "打开浏览器语音房")) {
			if (m_RoomOpen) { m_Input.Reset(); m_Message = "语音房已关闭。"; }
			else { m_Input.OpenRoom(); m_Message = "正在连接本机语音服务…"; }
			m_RoomOpen = !m_RoomOpen;
		}
		if (!m_Message.empty()) ImGui::TextWrapped("%s",m_Message.c_str());
		if (!m_Playback.Error().empty()) ImGui::TextWrapped("播报：%s", m_Playback.Error().c_str());
	}
}
