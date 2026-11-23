#include "Redflag/AI/SpeechPlayback.h"
#include "Redflag/AI/LocalService.h"
#include "Redflag/Core/Log.h"
#include <chrono>
#include <iostream>
#include <fstream>
#include <thread>

int main(int argc, char** argv)
{
	Redflag::Log::Init();
	const uint16_t port = argc > 1 ? uint16_t(std::stoi(argv[1])) : 8717;
	if (argc > 2 && (std::string(argv[2]) == "--chunks" || std::string(argv[2]) == "--legacy"))
	{
		Redflag::SpeechPlayback voice(port);
		Redflag::LocalService fixture(port);
		auto until = [&](auto ready) {
			const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(8);
			while (!ready() && voice.Error().empty() && std::chrono::steady_clock::now() < limit)
			{
				voice.Update();
				std::this_thread::sleep_for(std::chrono::milliseconds(4));
			}
			return ready() && voice.Error().empty();
		};
		std::string recording;
		int64_t samples = 0;
		auto observe = [&](const Redflag::SpeechPlayback::Event& event) {
			if (std::string(event.Name) == "finished") { recording = *event.Wav; samples = event.DeviceSamples; }
		};
		const std::string bank = "assets/audio/watt/dylan.voice.json";
		if (std::string(argv[2]) == "--legacy")
		{
			voice.Speak("旧服务整句返回", "", observe);
			if (!until([&]{ return !recording.empty(); }) || samples != 4800) return 1;
			voice.Speak("声音文件损坏", "", observe);
			until([&]{ return !voice.Error().empty(); });
			if (voice.Error() != "invalid_voice_wav" || voice.Playing()) return 1;
			recording.clear();
			voice.Speak("医疗包给你了。", bank, observe);
			if (!until([&]{ return !recording.empty(); })) return 1;
			std::ifstream source("assets/audio/watt/share.wav", std::ios::binary);
			if (recording != std::string(std::istreambuf_iterator<char>(source), {})) return 1;
			int64_t replayed = -1;
			voice.PlayRecording(recording, 2400, [&](const Redflag::SpeechPlayback::Event& event) {
				if (std::string(event.Name) == "finished") replayed = event.DeviceSamples;
			});
			if (!until([&]{ return replayed == 2400; })) return 1;
			std::cout << "PASS legacy whole WAV, invalid WAV recovery, offline voice byte-for-byte, 2400-sample replay\n";
			return 0;
		}
		voice.Speak("测试分块播放", bank, observe);
		if (!until([&]{ return voice.FirstAudioMs() >= 0; })) return 1;
		// 测试服务仍扣着后半句。只有首块已经进过真正的音频设备，才允许模型继续返回。
		fixture.Send(L"POST", L"/test/continue", "{}");
		if (!until([&]{ return !recording.empty(); })) return 1;
		if (samples != 9600 || recording.size() != size_t(44 + samples * 2)) return 1;
		for (size_t i = 44; i < recording.size(); i += 2)
			if (uint8_t(recording[i]) != (i < 44 + 9600 ? 145 : 67) || uint8_t(recording[i+1]) != 1) return 1;
		std::cout << "PASS first audio before releasing second block; WAV equals the 9600 device samples\n";

		voice.Speak("请求会一直等到打断", bank);
		if (!until([&]{ return voice.FirstAudioMs() >= 0; })) return 1;
		voice.Speak("接下来这一条", bank);
		if (!until([&]{ return voice.FirstAudioMs() >= 0; })) return 1;
		const auto state = fixture.Send(L"GET", L"/test/status");
		if (!state["cancelled"].as<bool>()) return 1;
		std::cout << "PASS cancelled stream released the synthesizer and the next line reached the device\n";
		voice.Speak("声音中途出错", bank);
		until([&]{ return !voice.Error().empty(); });
		if (voice.Error().empty() || voice.Playing()) return 1;
		voice.Speak("接下来这一条", bank);
		if (!until([&]{ return voice.FirstAudioMs() >= 0; })) return 1;
		std::cout << "PASS partial stream failure ends playback and following voice recovers\n";
		return 0;
	}
	const int count = argc > 2 ? std::stoi(argv[2]) : 3;
	Redflag::SpeechPlayback playback(port);
	for (int i = 0; i < count; ++i)
	{
		playback.Speak("我在这里，等你回来。");
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
		while (playback.FirstAudioMs() < 0 && std::chrono::steady_clock::now() < deadline && playback.Error().empty())
		{
			playback.Update();
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		if (playback.FirstAudioMs() < 0) { std::cerr << playback.Error(); return 1; }
		auto stopped = std::chrono::steady_clock::now();
		playback.Stop();
		double stopMs = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now() - stopped).count();
		std::cout << playback.FirstAudioMs() << ' ' << stopMs << '\n';
		if (playback.Playing()) return 1;
	}
	// 先取消还在合成的下一句话，确认稍后返回的 WAV 不会重新开始播放。
	playback.Speak("这段测试会在声音合成完成之前取消。");
	playback.Stop();
	for (int i = 0; i < 200; ++i)
	{
		playback.Update();
		if (playback.Playing()) return 1;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}
