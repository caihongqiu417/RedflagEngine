#include "rfpch.h"
#include "SpeechPlayback.h"
#include "LocalService.h"
#include <mmsystem.h>
#include <cstring>
#pragma comment(lib, "winmm.lib")

namespace Redflag
{
	// ---- Windows 默认播放设备 ----

	struct SpeechPlayback::Output
	{
		HWAVEOUT Device = nullptr;
		WAVEHDR Header{};
		std::string Wav;
		uint32_t FirstSample = 0;
		bool Prepared = false;

		explicit Output(std::string wav) : Wav(std::move(wav))
		{
			// 本机合成服务返回标准 44 字节 WAV 头，后面逐个存放 16 位单声道采样。
			uint32_t dataSize = 0, rate = 0;
			uint16_t pcm = 0, channels = 0, bits = 0;
			if (Wav.size() < 44 || Wav.compare(0, 4, "RIFF") || Wav.compare(8, 8, "WAVEfmt ") || Wav.compare(36, 4, "data"))
				throw std::runtime_error("invalid_voice_wav");
			std::memcpy(&pcm, Wav.data() + 20, 2);
			std::memcpy(&channels, Wav.data() + 22, 2);
			std::memcpy(&rate, Wav.data() + 24, 4);
			std::memcpy(&bits, Wav.data() + 34, 2);
			std::memcpy(&dataSize, Wav.data() + 40, 4);
			if (pcm != 1 || channels != 1 || rate != 16000 || bits != 16 || !dataSize || dataSize % 2 || dataSize != Wav.size() - 44)
				throw std::runtime_error("invalid_voice_wav");

			Header.lpData = Wav.data() + 44;
			Header.dwBufferLength = dataSize;
			const uint32_t sampleCount = dataSize / 2;
			for (; FirstSample < sampleCount; ++FirstSample)
			{
				int16_t sample;
				std::memcpy(&sample, Header.lpData + 2 * FirstSample, 2);
				// 满幅的 0.5% 用来避开合成器句首的底噪，只影响测量。
				if (std::abs(int(sample)) > 164) break;
			}
		}

		void Start()
		{
			WAVEFORMATEX format{ WAVE_FORMAT_PCM, 1, 16000, 32000, 2, 16, 0 };
			if (waveOutOpen(&Device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
				throw std::runtime_error("无法打开默认播放设备。");
			Prepared = waveOutPrepareHeader(Device, &Header, sizeof(Header)) == MMSYSERR_NOERROR;
			if (!Prepared || waveOutWrite(Device, &Header, sizeof(Header)) != MMSYSERR_NOERROR)
				throw std::runtime_error("无法向默认设备播放语音。");
		}

		bool ReachedVoice() const
		{
			MMTIME position{};
			position.wType = TIME_SAMPLES;
			if (waveOutGetPosition(Device, &position, sizeof(position)) != MMSYSERR_NOERROR) return false;
			switch (position.wType)
			{
				case TIME_SAMPLES: return position.u.sample > FirstSample;
				case TIME_BYTES: return position.u.cb / 2 > FirstSample;
				case TIME_MS: return position.u.ms * 16 > FirstSample;
				default: return false;
			}
		}

		~Output()
		{
			if (!Device) return;
			waveOutReset(Device);
			if (Prepared) waveOutUnprepareHeader(Device, &Header, sizeof(Header));
			waveOutClose(Device);
		}
	};

	// ---- 合成、播报与打断 ----

	SpeechPlayback::SpeechPlayback(uint16_t port) : m_Port(port) {}

	SpeechPlayback::~SpeechPlayback() = default;

	void SpeechPlayback::Speak(const std::string& text, const std::string& voice)
	{
		Stop();
		m_Start = std::chrono::steady_clock::now();
		m_FirstAudioMs = -1;
		m_Error.clear();
		m_Jobs.push_back({ m_Line, std::async(std::launch::async, [port = m_Port, text, voice] {
			YAML::Emitter out;
			out.SetStringFormat(YAML::DoubleQuoted);
			out << YAML::Flow << YAML::BeginMap << YAML::Key << "text" << YAML::Value << text
				<< YAML::Key << "voice" << YAML::Value << voice << YAML::EndMap;
			return LocalService(port, 17000).Download(L"/v1/speech/synthesize", out.c_str());
		}) });
	}

	void SpeechPlayback::Stop()
	{
		++m_Line;
		m_Output.reset();
	}

	void SpeechPlayback::Update()
	{
		for (auto job = m_Jobs.begin(); job != m_Jobs.end(); )
		{
			if (job->Wav.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++job; continue; }
			if (job->Line == m_Line)
			{
				try {
					m_Output = std::make_unique<Output>(job->Wav.get());
					m_Output->Start();
				} catch (const std::exception& error) {
					m_Output.reset();
					m_Error = error.what();
				}
			}
			job = m_Jobs.erase(job);
		}
		if (!m_Output) return;
		if (m_FirstAudioMs < 0 && m_Output->ReachedVoice())
			m_FirstAudioMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_Start).count();
		if (m_Output->Header.dwFlags & WHDR_DONE) m_Output.reset();
	}
}
