#pragma once
#include <cstdint>
#include <chrono>
#include <future>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Redflag
{
	/* 游戏线程调用 Update。合成在后台执行，停止后晚到的音频会被丢弃。 */
	class SpeechPlayback
	{
	public:
		struct Event
		{
			const char* Name;
			uint64_t Line;
			int64_t DeviceSamples; // 设备已读过的单声道采样数。查不到时为 -1。
			const std::string* Wav; // 仅提交设备时提供，调用返回后失效。
		};
		using Observer = std::function<void(const Event&)>;

		explicit SpeechPlayback(uint16_t port = 8717);
		~SpeechPlayback();

		void Speak(const std::string& text, const std::string& voice = {}, Observer observe = {});
		/// <summary>回听录制的 16 kHz 单声道 WAV。从开头播放设备实际读过的采样数。</summary>
		void PlayRecording(std::string wav, uint32_t samples, Observer observe = {});
		void Stop();
		void Update();

		/// <summary>从请求合成到系统音频设备读过首个有声采样的毫秒数。尚未读到时为 -1。</summary>
		double FirstAudioMs() const { return m_FirstAudioMs; }
		bool Playing() const { return bool(m_Output); }
		const std::string& Error() const { return m_Error; }

	private:
		struct Output;
		struct Job { uint64_t Line; std::future<std::string> Wav; };
		uint16_t m_Port;
		uint64_t m_Line = 0;
		std::chrono::steady_clock::time_point m_Start;
		std::vector<Job> m_Jobs;
		std::unique_ptr<Output> m_Output;
		Observer m_Observe;
		double m_FirstAudioMs = -1;
		std::string m_Error;
	};
}
