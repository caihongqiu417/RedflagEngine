workspace "Redflag"
	architecture "x64"
	startproject "Sandbox"
	configurations { "Debug", "Release", "Dist" }

-- 组成输出目录: Debug-windows-x86_64
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

project "Redflag"
	location "Redflag"
	kind "SharedLib"
	language "C++"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir    ("bin-int/" .. outputdir .. "/%{prj.name}")

	files {
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.hpp",
		"%{prj.name}/src/**.cpp"
	}

	includedirs {
		"%{prj.name}/src",
		"%{prj.name}/vendor/spdlog/include"
	}

	filter "system:windows"
		systemversion "latest"
		cppdialect "C++17"
		characterset "Unicode"
		staticruntime "On"                  -- /MT、/MTd
		buildoptions { "/utf-8" }           -- 关键：启用 UTF-8 源码
		disablewarnings { "4251", "4828" }  -- 4251: DLL 接口；4828: BOM/无效字符告警

		defines {
			"RF_PLATFORM_WINDOWS",
			"RF_BUILD_DLL"
		}

		-- 构建后复制 Redflag.dll 到 Sandbox 输出目录（先确保目标目录存在）
		postbuildcommands {
			("{MKDIR} ../bin/" .. outputdir .. "/Sandbox"),
			("{COPY} %{cfg.buildtarget.relpath} ../bin/" .. outputdir .. "/Sandbox")
		}

	filter "configurations:Debug"
		defines { "RF_DEBUG" }
		symbols "On"

	filter "configurations:Release"
		defines { "RF_RELEASE" }
		optimize "On"

	filter "configurations:Dist"
		defines { "RF_DIST" }
		optimize "On"

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

	includedirs {
		"Redflag/src",
		"Redflag/vendor/spdlog/include"
	}

	links { "Redflag" }

	filter "system:windows"
		systemversion "latest"
		cppdialect "C++17"
		characterset "Unicode"
		staticruntime "On"
		buildoptions { "/utf-8" }
		disablewarnings { "4251", "4828" }

		defines { "RF_PLATFORM_WINDOWS" }

	filter "configurations:Debug"
		defines { "RF_DEBUG" }
		symbols "On"

	filter "configurations:Release"
		defines { "RF_RELEASE" }
		optimize "On"

	filter "configurations:Dist"
		defines { "RF_DIST" }
		optimize "On"