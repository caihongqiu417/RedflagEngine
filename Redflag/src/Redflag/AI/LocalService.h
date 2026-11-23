#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
	/* 决策和语音客户端只与本机服务通信。模型上游地址与凭证由服务进程持有。 */
	class LocalService
	{
	public:
		explicit LocalService(uint16_t port, int receiveTimeout = 3000);
		static uint16_t SpeechPort();
		static uint16_t DecisionPort();
		YAML::Node Send(const wchar_t* method, const std::wstring& path, const std::string& body = {},
			const wchar_t* type = L"Content-Type: application/json; charset=utf-8");
		/// <summary>保留决策服务回应的 JSON 原文，方便在 Forge 对照模型请求和回复。</summary>
		std::string RequestJSON(const wchar_t* method, const std::wstring& path, const std::string& body = {});
		std::string Download(const std::wstring& path, const std::string& json);
		/// <summary>向语音服务读取一个音频块。200 携带 PCM，202 表示仍在合成，204 表示整句已读完。</summary>
		unsigned ReadAudio(const std::wstring& path, std::string& pcm);
		/// <summary>创建可取消的语音请求。旧服务只提供整句 WAV，这时返回空路径。</summary>
		std::wstring StartAudio(const std::string& json);

	private:
		struct Close { void operator()(void* handle) const; };
		using Handle = std::unique_ptr<void, Close>;
		Handle m_Session, m_Host;
		std::string Request(const wchar_t* method, const std::wstring& path, const std::string& body,
			const wchar_t* type, size_t limit, unsigned* statusCode = nullptr);
	};
}
