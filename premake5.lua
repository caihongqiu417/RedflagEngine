workspace "Redflag"
	architecture "x64"
	startproject "Sandbox"
	configurations { "Debug", "Release", "Dist" }

-- 输出目录格式: 配置-系统-架构
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

-- 全局定义 - 启用外部fmt
defines {
	"FMT_HEADER_ONLY",           -- 使用header-only模式的fmt
	"SPDLOG_FMT_EXTERNAL",       -- 告诉spdlog使用外部fmt
	"_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS"
}

-- Redflag引擎核心库项目
project "Redflag"
	location "Redflag"
	kind "SharedLib"
	language "C++"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir    ("bin-int/" .. outputdir .. "/%{prj.name}")

	-- 预编译头配置
	pchheader "hzpch.h"
	pchsource "Redflag/src/hzpch.cpp"

	-- 源文件包含
	files {
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.hpp", 
		"%{prj.name}/src/**.cpp",
		"%{prj.name}/vendor/fmt/src/format.cc"  -- 包含fmt的源文件
	}

	-- 头文件包含路径
	includedirs {
		"%{prj.name}/src",
		"%{prj.name}/vendor/spdlog/include",
		"%{prj.name}/vendor/fmt/include"         -- 添加fmt头文件路径
	}

	-- 为 fmt 源文件禁用预编译头（关键修复）
	filter "files:**/vendor/fmt/src/format.cc"
		flags { "NoPCH" }

	-- Windows平台特定配置
	filter "system:windows"
		systemversion "latest"
		cppdialect "C++17"
		characterset "Unicode"
		staticruntime "On"
		buildoptions { "/utf-8" }
		disablewarnings { 
			"4251", "4828", "4996", "5030"
		}

		defines {
			"RF_PLATFORM_WINDOWS",
			"RF_BUILD_DLL",
			"NOMINMAX",
			"WIN32_LEAN_AND_MEAN"
		}

		-- 构建后复制 DLL 到 Sandbox
		postbuildcommands {
			("{MKDIR} ../bin/" .. outputdir .. "/Sandbox"),
			("{COPY} %{cfg.buildtarget.relpath} ../bin/" .. outputdir .. "/Sandbox")
		}

	-- 调试配置
	filter "configurations:Debug"
		defines { "RF_DEBUG", "RF_ENABLE_LOGGING" }
		symbols "On"
		runtime "Debug"

	-- 发布配置
	filter "configurations:Release"
		defines { "RF_RELEASE", "RF_ENABLE_LOGGING" }
		optimize "On"
		runtime "Release"

	-- 分发配置
	filter "configurations:Dist"
		defines { "RF_DIST" }
		optimize "On"
		runtime "Release"

-- Sandbox测试应用项目
project "Sandbox"
	location "Sandbox"
	kind "ConsoleApp"
	language "C++"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir    ("bin-int/" .. outputdir .. "/%{prj.name}")

	-- 源文件包含
	files {
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.hpp",
		"%{prj.name}/src/**.cpp"
	}

	-- 头文件包含路径
	includedirs {
		"Redflag/src",
		"Redflag/vendor/spdlog/include",
		"Redflag/vendor/fmt/include"  -- 添加fmt头文件路径
	}

	-- 链接依赖
	links { "Redflag" }

	-- Windows平台特定配置
	filter "system:windows"
		systemversion "latest"
		cppdialect "C++17"
		characterset "Unicode"
		staticruntime "On"
		buildoptions { "/utf-8" }
		disablewarnings { 
			"4251", "4828", "4996", "5030"
		}

		defines {
			"RF_PLATFORM_WINDOWS",
			"NOMINMAX",
			"WIN32_LEAN_AND_MEAN"
		}

	-- 调试配置
	filter "configurations:Debug"
		defines { "RF_DEBUG", "RF_ENABLE_LOGGING" }
		symbols "On"
		runtime "Debug"

	-- 发布配置
	filter "configurations:Release"
		defines { "RF_RELEASE", "RF_ENABLE_LOGGING" }
		optimize "On"
		runtime "Release"

	-- 分发配置
	filter "configurations:Dist"
		defines { "RF_DIST" }
		optimize "On"
		runtime "Release"