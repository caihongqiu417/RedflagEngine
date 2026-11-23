#include "rfpch.h"
#include "LocalService.h"
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

namespace Redflag
{
	void LocalService::Close::operator()(void* handle) const { WinHttpCloseHandle(handle); }

	static uint16_t ConfiguredPort(const char* name, uint16_t fallback)
	{
		const char* setting = std::getenv(name);
		if (setting)
		{
			uint16_t port = 0;
			const char* end = setting + std::strlen(setting);
			const auto value = std::from_chars(setting, end, port);
			if (value.ec == std::errc{} && value.ptr == end && port) return port;
		}
		return fallback;
	}

	uint16_t LocalService::SpeechPort() { return ConfiguredPort("REDFLAG_SPEECH_PORT", 8717); }
	uint16_t LocalService::DecisionPort() { return ConfiguredPort("REDFLAG_DECISION_PORT", 8716); }

	LocalService::LocalService(uint16_t port, int receiveTimeout)
	{
		m_Session.reset(WinHttpOpen(L"Redflag", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0));
		if (!m_Session) throw std::runtime_error("service_unavailable");
		WinHttpSetTimeouts(m_Session.get(), 2000, 2000, 3000, receiveTimeout);
		m_Host.reset(WinHttpConnect(m_Session.get(), L"127.0.0.1", port, 0));
		if (!m_Host) throw std::runtime_error("service_unavailable");
	}

	YAML::Node LocalService::Send(const wchar_t* method, const std::wstring& path, const std::string& body, const wchar_t* type)
	{
		return YAML::Load(Request(method, path, body, type, 65536));
	}

	std::string LocalService::RequestJSON(const wchar_t* method, const std::wstring& path, const std::string& body)
	{
		// 决策服务接受最多 12 MiB 的图文输入；调试回应还会写入模型的选择和请求信息。
		return Request(method, path, body, L"Content-Type: application/json; charset=utf-8", 16 * 1024 * 1024);
	}

	std::string LocalService::Download(const std::wstring& path, const std::string& json)
	{
		// 每秒单声道语音占 32 KB，给最多 300 字的回复留出 5 分钟。
		return Request(L"POST", path, json, L"Content-Type: application/json; charset=utf-8", 32000 * 300);
	}

	std::wstring LocalService::StartAudio(const std::string& json)
	{
		unsigned status = 0;
		const std::string data = Request(L"POST", L"/v1/speech/streams", json, L"Content-Type: application/json; charset=utf-8", 65536, &status);
		if (status == 404 || status == 405) return {};
		const YAML::Node reply = YAML::Load(data);
		if (status != 201) throw std::runtime_error(reply["error"].as<std::string>("speech_request_failed"));
		const std::string id = reply["id"].as<std::string>();
		return L"/v1/speech/streams/" + std::wstring(id.begin(), id.end());
	}

	unsigned LocalService::ReadAudio(const std::wstring& path, std::string& pcm)
	{
		unsigned status = 0;
		pcm = Request(L"GET", path, {}, nullptr, 32000 * 300, &status);
		if (status != 200 && status != 202 && status != 204)
			throw std::runtime_error(YAML::Load(pcm)["error"].as<std::string>("speech_stream_failed"));
		return status;
	}

	std::string LocalService::Request(const wchar_t* method, const std::wstring& path, const std::string& body,
		const wchar_t* type, size_t limit, unsigned* statusCode)
	{
		Handle request(WinHttpOpenRequest(m_Host.get(), method, path.c_str(), nullptr, nullptr, nullptr, 0));
		DWORD disable = WINHTTP_DISABLE_REDIRECTS;
		if (!request || !WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable))
			|| !WinHttpSendRequest(request.get(), type, type ? DWORD(-1) : 0,
				body.empty() ? nullptr : const_cast<char*>(body.data()), DWORD(body.size()), DWORD(body.size()), 0)
			|| !WinHttpReceiveResponse(request.get(), nullptr))
			throw std::runtime_error("service_unavailable");
		DWORD status = 0, size = sizeof(status);
		if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			nullptr, &status, &size, nullptr))
			throw std::runtime_error("service_unavailable");
		std::string reply;
		char bytes[2048];
		DWORD count = 0;
		do {
			if (!WinHttpReadData(request.get(), bytes, sizeof(bytes), &count) || reply.size() + count > limit)
				throw std::runtime_error("invalid_service_reply");
			reply.append(bytes, count);
		} while (count);
		if (statusCode) *statusCode = status;
		else if (status < 200 || status >= 300)
			throw std::runtime_error(YAML::Load(reply)["error"].as<std::string>("service_error"));
		return reply;
	}
}
