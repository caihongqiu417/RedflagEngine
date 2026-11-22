#include "rfpch.h"
#include "LocalService.h"
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

namespace Redflag
{
	void LocalService::Close::operator()(void* handle) const { WinHttpCloseHandle(handle); }

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

	std::string LocalService::Download(const std::wstring& path, const std::string& json)
	{
		// 每秒单声道语音占 32 KB，给最多 300 字的回复留出 5 分钟。
		return Request(L"POST", path, json, L"Content-Type: application/json; charset=utf-8", 32000 * 300);
	}

	std::string LocalService::Request(const wchar_t* method, const std::wstring& path, const std::string& body,
		const wchar_t* type, size_t limit)
	{
		Handle request(WinHttpOpenRequest(m_Host.get(), method, path.c_str(), nullptr, nullptr, nullptr, 0));
		DWORD disable = WINHTTP_DISABLE_REDIRECTS;
		if (!request || !WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable))
			|| !WinHttpSendRequest(request.get(), type, -1,
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
		if (status < 200 || status >= 300)
			throw std::runtime_error(YAML::Load(reply)["error"].as<std::string>("service_error"));
		return reply;
	}
}
