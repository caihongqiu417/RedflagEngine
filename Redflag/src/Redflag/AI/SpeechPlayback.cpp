#include "rfpch.h"
#include "SpeechPlayback.h"
#include "LocalService.h"
#include "Redflag/Core/Log.h"
#include <atomic>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mmsystem.h>
#include <mutex>
#include <string_view>
#pragma comment(lib, "winmm.lib")

namespace Redflag
{
	//========== 16 kHz 单声道录音 ==========

	struct VoiceWavHeader
	{
		char Riff[4] = {'R','I','F','F'};
		uint32_t FileSize = 36;
		char WaveFormat[8] = {'W','A','V','E','f','m','t',' '};
		uint32_t FormatSize = 16;
		uint16_t PCM = 1, Channels = 1;
		uint32_t Rate = 16000, BytesPerSecond = 32000;
		uint16_t Block = 2, Bits = 16;
		char Data[4] = {'d','a','t','a'};
		uint32_t Bytes = 0;
	};
	static_assert(sizeof(VoiceWavHeader) == 44);

	static std::string ReadWav(const std::string& wav)
	{
		VoiceWavHeader header;
		if (wav.size() < sizeof(header)) throw std::runtime_error("invalid_voice_wav");
		std::memcpy(&header, wav.data(), sizeof(header));
		if (wav.compare(0, 4, "RIFF") || wav.compare(8, 8, "WAVEfmt ") || wav.compare(36, 4, "data") ||
			header.FormatSize != 16 || header.PCM != 1 || header.Channels != 1 || header.Rate != 16000 ||
			header.Bits != 16 || !header.Bytes || header.Bytes % 2 || header.Bytes != wav.size() - sizeof(header))
			throw std::runtime_error("invalid_voice_wav");
		return wav.substr(sizeof(header));
	}

	static void AppendRecording(std::string& wav, const std::string& pcm)
	{
		VoiceWavHeader header;
		if (wav.empty()) wav.resize(sizeof(header));
		wav.append(pcm);
		header.Bytes = uint32_t(wav.size() - sizeof(header));
		header.FileSize += header.Bytes;
		std::memcpy(wav.data(), &header, sizeof(header));
	}

	//========== 同一句话的声音连续交给声卡 ==========

	struct SpeechPlayback::Output
	{
		struct Block { WAVEHDR Header{}; std::string Pcm; };
		HWAVEOUT Device = nullptr;
		std::deque<Block> Blocks;
		int64_t Submitted = 0, FirstVoice = -1;

		Output()
		{
			WAVEFORMATEX format{WAVE_FORMAT_PCM, 1, 16000, 32000, 2, 16, 0};
			if (waveOutOpen(&Device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
				throw std::runtime_error("无法打开默认播放设备。");
		}

		void Append(std::string pcm)
		{
			if (pcm.empty() || pcm.size() % 2) throw std::runtime_error("invalid_voice_pcm");
			Block& block = Blocks.emplace_back();
			block.Pcm = std::move(pcm);
			block.Header.lpData = block.Pcm.data();
			block.Header.dwBufferLength = DWORD(block.Pcm.size());
			if (waveOutPrepareHeader(Device, &block.Header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR ||
				waveOutWrite(Device, &block.Header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
				throw std::runtime_error("无法向默认设备播放语音。");

			// 只影响计时。声音首次超过满幅的 0.5% 时，记下它在整段台词里的采样位置。
			for (size_t i = 0; FirstVoice < 0 && i < block.Pcm.size(); i += 2)
			{
				int16_t value;
				std::memcpy(&value, block.Pcm.data() + i, sizeof(value));
				if (std::abs(int(value)) > 164) FirstVoice = Submitted + int64_t(i / 2);
			}
			Submitted += int64_t(block.Pcm.size() / 2);
		}

		int64_t Position() const
		{
			MMTIME value{};
			value.wType = TIME_SAMPLES;
			if (waveOutGetPosition(Device, &value, sizeof(value)) != MMSYSERR_NOERROR) return -1;
			switch (value.wType)
			{
				case TIME_SAMPLES: return value.u.sample;
				case TIME_BYTES: return value.u.cb / 2;
				case TIME_MS: return int64_t(value.u.ms) * 16;
				default: return -1;
			}
		}

		void Reclaim()
		{
			while (!Blocks.empty() && (Blocks.front().Header.dwFlags & WHDR_DONE))
			{
				waveOutUnprepareHeader(Device, &Blocks.front().Header, sizeof(WAVEHDR));
				Blocks.pop_front();
			}
		}

		~Output()
		{
			waveOutReset(Device);
			for (Block& block : Blocks)
				if (block.Header.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(Device, &block.Header, sizeof(WAVEHDR));
			waveOutClose(Device);
		}
	};

	//========== 后台合成与取消 ==========

	struct SpeechPlayback::Request
	{
		struct Part { std::string Pcm; size_t End; };
		std::atomic<bool> Cancelled{false};
		std::mutex Mutex;
		std::deque<Part> Parts;
		std::string Error;
		bool Finished = false;

		void Push(std::string pcm, size_t end)
		{
			std::lock_guard<std::mutex> lock(Mutex);
			Parts.push_back({std::move(pcm), end});
		}
	};

	static std::string BankRecording(const std::string& voice, const std::string& text)
	{
		const std::filesystem::path bank = std::filesystem::u8path(voice);
		if (bank.extension() != ".json" || bank.stem().extension() != ".voice") return {};
		std::ifstream index(bank);
		if (!index) throw std::runtime_error("找不到角色的音色文件。");
		const YAML::Node line = YAML::Load(index)["Lines"][text];
		if (!line) return {};
		std::ifstream wav(bank.parent_path() / std::filesystem::u8path(line.as<std::string>()), std::ios::binary);
		if (!wav) throw std::runtime_error("找不到这句台词的 WAV。");
		return ReadWav({std::istreambuf_iterator<char>(wav), {}});
	}

	void SpeechPlayback::Generate(const std::shared_ptr<Request>& request, uint16_t port, std::string text, std::string voice)
	{
		std::string error;
		try
		{
			std::string cached = BankRecording(voice, text);
			if (!cached.empty())
			{
				request->Push(std::move(cached), text.size());
				request->Push({}, text.size());
			}
			else
			{
				LocalService service(port, 90000);
				size_t begin = 0;
				while (begin < text.size() && !request->Cancelled)
				{
					size_t end = text.size();
					for (std::string_view mark : {"。","？","！","；","?","!","\n"})
					{
						const size_t at = text.find(mark, begin);
						if (at != std::string::npos) end = std::min(end, at + mark.size());
					}
					const std::string sentence = text.substr(begin, end - begin);
					begin = end;
					if (sentence.find_first_not_of(" \t\r\n") == std::string::npos) continue;
					YAML::Emitter json;
					json.SetStringFormat(YAML::DoubleQuoted);
					json << YAML::Flow << YAML::BeginMap << YAML::Key << "text" << YAML::Value << sentence
						<< YAML::Key << "voice" << YAML::Value << voice << YAML::EndMap;

					const std::wstring stream = service.StartAudio(json.c_str());
					if (stream.empty())
						request->Push(ReadWav(service.Download(L"/v1/speech/synthesize", json.c_str())), end);
					else
					{
						while (!request->Cancelled)
						{
							std::string pcm;
							const unsigned status = service.ReadAudio(stream, pcm);
							if (status == 204) break;
							if (status == 200) request->Push(std::move(pcm), end);
						}
						if (request->Cancelled)
						{
							service.Send(L"DELETE", stream);
							break;
						}
					}
					request->Push({}, end); // 这句已经生成完，字幕可以按完整句长计算播放进度。
				}
			}
		}
		catch (const std::exception& failure) { error = failure.what(); }
		std::lock_guard<std::mutex> lock(request->Mutex);
		request->Error = std::move(error);
		request->Finished = true;
	}

	//========== 游戏线程播放、字幕与回听 ==========

	SpeechPlayback::SpeechPlayback(uint16_t port) : m_Port(port ? port : LocalService::SpeechPort()) {}
	SpeechPlayback::~SpeechPlayback() { Stop(); }

	void SpeechPlayback::Speak(const std::string& text, const std::string& voice, Observer observe)
	{
		Stop();
		const size_t first = text.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) return;
		std::string line = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
		m_TextSize = line.size();
		m_Observe = std::move(observe);
		m_Start = std::chrono::steady_clock::now();
		m_FirstAudioMs = -1;
		m_Error.clear();
		if (m_Observe) m_Observe({"requested", m_Line, -1, nullptr});
		m_Request = std::make_shared<Request>();
		m_Workers.push_back(std::async(std::launch::async, Generate, m_Request, m_Port, std::move(line), voice));
	}

	void SpeechPlayback::Queue(std::string pcm, size_t end)
	{
		if (pcm.empty())
		{
			if (!m_Phrases.empty() && m_Phrases.back().End == end) m_Phrases.back().Complete = true;
			return;
		}
		if (!m_Output) m_Output = std::make_unique<Output>();
		m_Output->Append(std::move(pcm));
		if (m_EmptySince != std::chrono::steady_clock::time_point{})
		{
			const double gap = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_EmptySince).count();
			if (m_Observe) m_Observe({"buffer_gap", m_Line, m_Output->Position(), nullptr, gap});
			m_EmptySince = {};
		}
		if (m_Phrases.empty() || m_Phrases.back().End != end) m_Phrases.push_back({end, 0, false});
		m_Phrases.back().Samples = m_Output->Submitted;
		if (m_Observe)
		{
			AppendRecording(m_Recorded, m_Output->Blocks.back().Pcm);
			if (!m_Active) m_Observe({"submitted", m_Line, 0, &m_Recorded});
		}
		m_Active = true;
	}

	void SpeechPlayback::PlayRecording(std::string wav, uint32_t samples, Observer observe)
	{
		Stop();
		if (!samples) return;
		std::string pcm = ReadWav(wav);
		if (samples > pcm.size() / 2) throw std::runtime_error("记录的采样数超过 WAV 中的音频长度。");
		pcm.resize(size_t(samples) * 2);
		auto output = std::make_unique<Output>();
		output->Append(std::move(pcm));
		m_Output = std::move(output);
		m_Active = true;
		m_Observe = std::move(observe);
		m_Start = std::chrono::steady_clock::now();
		m_FirstAudioMs = -1;
		m_Error.clear();
	}

	float SpeechPlayback::PlaybackProgress() const
	{
		if (!m_Active || !m_Output) return -1.f;
		const int64_t played = std::max<int64_t>(0, m_Output->Position());
		if (!m_TextSize) return std::min(1.f, float(played) / m_Output->Submitted);
		size_t begin = 0;
		int64_t from = 0;
		for (const Phrase& phrase : m_Phrases)
		{
			if (!phrase.Complete) return float(begin) / m_TextSize;
			if (played < phrase.Samples)
				return (begin + float(played - from) / float(phrase.Samples - from) * (phrase.End - begin)) / m_TextSize;
			begin = phrase.End;
			from = phrase.Samples;
		}
		return float(begin) / m_TextSize;
	}

	void SpeechPlayback::Stop()
	{
		if (m_Request) m_Request->Cancelled = true;
		m_Request.reset();
		const int64_t samples = m_Output ? m_Output->Position() : -1;
		m_Output.reset();
		if (m_Observe) m_Observe({m_Active ? "stopped" : "cancelled", m_Line, samples, m_Recorded.empty() ? nullptr : &m_Recorded});
		m_Active = false;
		m_Recorded.clear();
		m_Phrases.clear();
		m_EmptySince = {};
		m_TextSize = 0;
		m_Observe = {};
		++m_Line;
	}

	void SpeechPlayback::Update()
	{
		for (auto it = m_Workers.begin(); it != m_Workers.end(); )
			if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) it = m_Workers.erase(it);
			else ++it;

		if (m_Request)
		{
			std::deque<Request::Part> parts;
			bool finished;
			{
				std::lock_guard<std::mutex> lock(m_Request->Mutex);
				parts.swap(m_Request->Parts);
				finished = m_Request->Finished;
				if (finished) m_Error = m_Request->Error;
			}
			try
			{
				for (Request::Part& part : parts) Queue(std::move(part.Pcm), part.End);
			}
			catch (const std::exception& failure) { m_Error = failure.what(); }
			if (!m_Error.empty() || (finished && !m_Output))
			{
				if (m_Error.empty()) m_Error = "没有收到可播放的声音。";
				RF_CORE_WARN("Speech playback: {0}", m_Error);
				if (m_Observe) m_Observe({"failed", m_Line, m_Output ? m_Output->Position() : -1, m_Recorded.empty() ? nullptr : &m_Recorded});
				m_Observe = {};
				Stop();
				return;
			}
			if (finished) m_Request.reset();
		}
		if (!m_Output) return;
		const int64_t position = m_Output->Position();
		if (m_FirstAudioMs < 0 && m_Output->FirstVoice >= 0 && position > m_Output->FirstVoice)
		{
			m_FirstAudioMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_Start).count();
			if (m_Observe) m_Observe({"first_voice", m_Line, position, nullptr});
		}
		m_Output->Reclaim();
		if (m_Request && m_Output->Blocks.empty() && m_EmptySince == std::chrono::steady_clock::time_point{})
			m_EmptySince = std::chrono::steady_clock::now();
		if (!m_Request && m_Output->Blocks.empty())
		{
			if (m_Observe) m_Observe({"finished", m_Line, m_Output->Submitted, m_Recorded.empty() ? nullptr : &m_Recorded});
			m_Observe = {};
			m_Active = false;
			m_Output.reset();
		}
	}
}
