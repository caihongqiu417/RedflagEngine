#include "Log.h"

namespace Redflag
{
	std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

	void Log::Init()
	{

		spdlog::set_pattern("%^[%T] %n: %v%$");


		//引擎端
		//创建多线程logger
		s_CoreLogger = spdlog::stdout_color_mt("REDFLAG");
		//设置打印消息的级别，trace是打印所有东西
		s_CoreLogger->set_level(spdlog::level::level_enum::trace);


		//客户端
		//...
		s_ClientLogger = spdlog::stdout_color_mt("APP");
		s_ClientLogger->set_level(spdlog::level::level_enum::trace);


	}
}