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

	-- 源文件包含 - 添加fmt源文件
	files {
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.hpp", 
		"%{prj.name}/src/**.cpp",
		"%{prj.name}/vendor/fmt/src/format.cc",  -- 包含fmt的源文件
		"%{prj.name}/vendor/fmt/include/**.h"    -- 包含fmt的头文件
	}

	-- 头文件包含路径 - 添加fmt包含目录
	includedirs {
		"%{prj.name}/src",
		"%{prj.name}/vendor/spdlog/include",
		"%{prj.name}/vendor/fmt/include"         -- 添加fmt头文件路径
	}

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

		postbuildcommands {
			("{MKDIR} ../bin/" .. outputdir .. "/Sandbox"),
			("{COPY} %{cfg.buildtarget.relpath} ../bin/" .. outputdir .. "/Sandbox")
		}

	filter "configurations:Debug"
		defines { "RF_DEBUG", "RF_ENABLE_LOGGING" }
		symbols "On"
		runtime "Debug"

	filter "configurations:Release"
		defines { "RF_RELEASE", "RF_ENABLE_LOGGING" }
		optimize "On"
		runtime "Release"

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

	files {
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.hpp",
		"%{prj.name}/src/**.cpp"
	}

	-- 头文件包含路径 - 同样添加fmt路径
	includedirs {
		"Redflag/src",
		"Redflag/vendor/spdlog/include",
		"Redflag/vendor/fmt/include"  -- 添加fmt头文件路径
	}

	links { "Redflag" }

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

	filter "configurations:Debug"
		defines { "RF_DEBUG", "RF_ENABLE_LOGGING" }
		symbols "On"
		runtime "Debug"

	filter "configurations:Release"
		defines { "RF_RELEASE", "RF_ENABLE_LOGGING" }
		optimize "On"
		runtime "Release"

	filter "configurations:Dist"
		defines { "RF_DIST" }
		optimize "On"
		runtime "Release"