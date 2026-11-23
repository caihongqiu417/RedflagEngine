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
			const std::string* Wav; // 首句提交、结束或打断时提供已送进声卡的录音，仅在回调内有效。
			double GapMs = 0; // buffer_gap 记录游戏发现声卡播空到下一块重新提交经过的毫秒数。
		};
		using Observer = std::function<void(const Event&)>;

		explicit SpeechPlayback(uint16_t port = 0);
		~SpeechPlayback();

		void Speak(const std::string& text, const std::string& voice = {}, Observer observe = {});
		/// <summary>回听录制的 16 kHz 单声道 WAV。从开头播放设备实际读过的采样数。</summary>
		void PlayRecording(std::string wav, uint32_t samples, Observer observe = {});
		void Stop();
		void Update();

		/// <summary>从请求合成到系统音频设备读过首个有声采样的毫秒数。尚未读到时为 -1。</summary>
		double FirstAudioMs() const { return m_FirstAudioMs; }
		bool Playing() const { return m_Active; }
		/// <summary>字幕使用的台词进度。整句生成前停在句首，生成后按设备播放的采样数前进。</summary>
		float PlaybackProgress() const;
		const std::string& Error() const { return m_Error; }

	private:
		struct Output;
		struct Request;
		struct Phrase { size_t End; int64_t Samples; bool Complete; };
		static void Generate(const std::shared_ptr<Request>& request, uint16_t port, std::string text, std::string voice);
		void Queue(std::string pcm, size_t end);
		uint16_t m_Port;
		uint64_t m_Line = 0;
		std::chrono::steady_clock::time_point m_Start;
		std::chrono::steady_clock::time_point m_EmptySince{};
		std::shared_ptr<Request> m_Request;
		std::vector<std::future<void>> m_Workers;
		std::unique_ptr<Output> m_Output;
		std::vector<Phrase> m_Phrases;
		Observer m_Observe;
		double m_FirstAudioMs = -1;
		std::string m_Error;
		std::string m_Recorded;
		size_t m_TextSize = 0;
		bool m_Active = false;
	};
}
