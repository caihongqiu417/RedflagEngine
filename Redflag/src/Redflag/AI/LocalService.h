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
		YAML::Node Send(const wchar_t* method, const std::wstring& path, const std::string& body = {},
			const wchar_t* type = L"Content-Type: application/json; charset=utf-8");
		std::string Download(const std::wstring& path, const std::string& json);

	private:
		struct Close { void operator()(void* handle) const; };
		using Handle = std::unique_ptr<void, Close>;
		Handle m_Session, m_Host;
		std::string Request(const wchar_t* method, const std::wstring& path, const std::string& body,
			const wchar_t* type, size_t limit);
	};
}
