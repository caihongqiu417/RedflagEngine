#pragma once

#include "Core.h"
#include"spdlog/spdlog.h"
#include"memory.h"

#include "spdlog/sinks/stdout_color_sinks.h" 

namespace Redflag {
	class REDFLAG_API Log
	{
	public:
		static void Init();

		inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_ClientLogger;
	};
}


// Core 的 log 简化
#define RF_CORE_ERROR(...)      ::Redflag::Log::GetCoreLogger()->error(__VA_ARGS__)
#define RF_CORE_WARN(...)       ::Redflag::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define RF_CORE_INFO(...)       ::Redflag::Log::GetCoreLogger()->info(__VA_ARGS__)
#define RF_CORE_TRACE(...)      ::Redflag::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define RF_CORE_FATAL(...)      ::Redflag::Log::GetCoreLogger()->fatal(__VA_ARGS__)

// Client 的 log 简化
#define RF_CLIENT_ERROR(...)    ::Redflag::Log::GetClientLogger()->error(__VA_ARGS__)
#define RF_CLIENT_WARN(...)     ::Redflag::Log::GetClientLogger()->warn(__VA_ARGS__)
#define RF_CLIENT_INFO(...)     ::Redflag::Log::GetClientLogger()->info(__VA_ARGS__)
#define RF_CLIENT_TRACE(...)    ::Redflag::Log::GetClientLogger()->trace(__VA_ARGS__)
#define RF_CLIENT_FATAL(...)    ::Redflag::Log::GetClientLogger()->fatal(__VA_ARGS__)
