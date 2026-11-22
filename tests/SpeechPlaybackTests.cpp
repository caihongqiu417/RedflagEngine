#include "Redflag/AI/SpeechPlayback.h"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv)
{
	const uint16_t port = argc > 1 ? uint16_t(std::stoi(argv[1])) : 8717;
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
