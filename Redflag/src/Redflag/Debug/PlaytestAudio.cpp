#include "rfpch.h"
#include "PlaytestAudio.h"
#include "PlaytestSession.h"
#include <fstream>
#include <iomanip>

namespace Redflag
{
	SpeechPlayback::Observer ObserveGameSpeech(uint64_t speaker)
	{
		const PlaytestSession* session = PlaytestSession::Active();
		if (!session) return {};
		const PlaytestInfo& start = session->Info();

		// 只有游戏线程提交、检查或停止播放时才写记录。后台合成线程不接触试玩会话。
		return [speaker, directory = start.Directory, seq = start.Seq, saved = false](const SpeechPlayback::Event& event) mutable {
			const PlaytestSession* active = PlaytestSession::Active();
			if (!active || active->Info().Directory != directory || active->Info().Seq < seq) return;
			const PlaytestInfo& now = active->Info();
			const std::string id = std::to_string(seq) + "-" + std::to_string(speaker) + "-" + std::to_string(event.Line);
			const std::string file = "speech-" + id + ".wav";
			if (event.Wav)
			{
				std::ofstream wav(directory / file, std::ios::binary);
				wav.write(event.Wav->data(), event.Wav->size());
				wav.close();
				saved = bool(wav);
			}

			std::ofstream log(directory / "media.jsonl", std::ios::app);
			log << std::setprecision(12) << "{\"kind\":\"audio\",\"event\":\"" << event.Name << "\",\"id\":\"" << id
				<< "\",\"entity\":\"" << speaker << "\",\"seq\":" << now.Seq << ",\"seconds\":" << now.Seconds
				<< ",\"generation\":" << now.Generation << ",\"rate_hz\":16000,\"file\":";
			if (saved) log << '\"' << file << '\"'; else log << "null";
			log << ",\"device_samples\":";
			if (event.DeviceSamples >= 0) log << event.DeviceSamples; else log << "null";
			log << "}\n";
		};
	}
}
