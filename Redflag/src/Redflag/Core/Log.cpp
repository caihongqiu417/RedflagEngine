#include"rfpch.h"
#include "Log.h"

namespace Redflag
{
	std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

	void Log::Init()
	{

        // ==================== 1. 优美的格式 ====================
// 简洁格式：时间 + 模块名 + 消息（带颜色）
        spdlog::set_pattern("%^[%T] %-8n: %v%$");
        // %-8n 让模块名对齐到8字符宽度，更美观

        // ==================== 2. 创建 Logger ====================
        s_CoreLogger = spdlog::stdout_color_mt("REDFLAG");
        s_ClientLogger = spdlog::stdout_color_mt("APP");

        // ==================== 3. 设置日志级别 ====================
        s_CoreLogger->set_level(spdlog::level::trace);
        s_ClientLogger->set_level(spdlog::level::trace);

        // ==================== 4. 精美的颜色配置 ====================
        // 获取 sink 进行颜色定制
        auto core_sink = static_cast<spdlog::sinks::stdout_color_sink_mt*>(
            s_CoreLogger->sinks()[0].get());
        auto client_sink = static_cast<spdlog::sinks::stdout_color_sink_mt*>(
            s_ClientLogger->sinks()[0].get());

        // ===== 核心端颜色（专业深色系）=====
        // TRACE: 深青色 - 用于详细调试
        core_sink->set_color(spdlog::level::trace,
            FOREGROUND_BLUE | FOREGROUND_GREEN);

        // DEBUG: 灰色 - 普通调试信息
        core_sink->set_color(spdlog::level::debug,
            FOREGROUND_INTENSITY);

        // INFO: 白色 - 重要信息（如引擎事件）
        core_sink->set_color(spdlog::level::info,
            FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

        // WARN: 橙色（红+黄） - 警告
        core_sink->set_color(spdlog::level::warn,
            FOREGROUND_RED | FOREGROUND_GREEN);

        // ERROR: 亮红色 - 错误
        core_sink->set_color(spdlog::level::err,
            FOREGROUND_RED | FOREGROUND_INTENSITY);

        // CRITICAL: 紫色（红+蓝） - 严重错误
        core_sink->set_color(spdlog::level::critical,
            FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);

        // ===== 客户端颜色（清新亮色系）=====
        // TRACE: 青色 - 客户端详细日志
        client_sink->set_color(spdlog::level::trace,
            FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);

        // DEBUG: 灰色 - 客户端调试
        client_sink->set_color(spdlog::level::debug,
            FOREGROUND_INTENSITY);

        // INFO: 蓝色 - 客户端信息（如Update）
        client_sink->set_color(spdlog::level::info,
            FOREGROUND_BLUE | FOREGROUND_INTENSITY);

        // WARN: 黄色 - 客户端警告
        client_sink->set_color(spdlog::level::warn,
            FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);

        // ERROR: 红色 - 客户端错误
        client_sink->set_color(spdlog::level::err,
            FOREGROUND_RED | FOREGROUND_INTENSITY);

        // CRITICAL: 洋红色 - 客户端严重错误
        client_sink->set_color(spdlog::level::critical,
            FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);

	}
}