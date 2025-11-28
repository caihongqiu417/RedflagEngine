workspace "Redflag"
    architecture "x64"
    startproject "Sandbox"
    configurations { "Debug", "Release", "Dist" }

-- 输出目录格式: 配置-系统-架构
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

-- ================================
-- 第 1 步：设置第三方库头文件路径
-- ================================
IncludeDir = {}
IncludeDir["GLFW"] = "Redflag/vendor/GLFW/include"

-- ================================
-- 第 2 步：加载 GLFW 子项目（必须）
-- 让 Premake 构建 GLFW 生成 GLFW.lib
-- ================================
include "Redflag/vendor/GLFW"

-- ================================
-- 全局定义 - 对 fmt/spdlog 的设置
-- ================================
defines {
    "FMT_HEADER_ONLY",                 -- fmt 使用 header-only 模式
    "SPDLOG_FMT_EXTERNAL",             -- spdlog 使用外部 fmt
    "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS"
}

-- ===========================================
--   Redflag 引擎核心库项目（DLL）
-- ===========================================
project "Redflag"
    location "Redflag"
    kind "SharedLib"
    language "C++"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir    ("bin-int/" .. outputdir .. "/%{prj.name}")

    -- 预编译头
    pchheader "hzpch.h"
    pchsource "Redflag/src/hzpch.cpp"

    -- 项目源文件
    files {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.hpp",
        "%{prj.name}/src/**.cpp",
        "%{prj.name}/vendor/fmt/src/format.cc"     -- fmt 源码文件
    }

    -- 头文件包含目录
    includedirs {
        "%{prj.name}/src",
        "%{prj.name}/vendor/spdlog/include",
        "%{IncludeDir.GLFW}",                      -- GLFW 头文件
        "%{prj.name}/vendor/fmt/include"
    }

    -- 链接到 GLFW + Windows OpenGL 系统库
    links {
        "GLFW",                                    -- GLFW 子项目生成的 lib
        "opengl32.lib"                             -- Windows 系统库
    }

    -- fmt 不使用预编译头
    filter "files:**/vendor/fmt/src/format.cc"
        flags { "NoPCH" }

    -- Windows 配置
    filter "system:windows"
        systemversion "latest"
        cppdialect "C++17"
        characterset "Unicode"
        staticruntime "On"
        buildoptions { "/utf-8" }
        disablewarnings { "4251", "4828", "4996", "5030" }

        defines {
            "RF_PLATFORM_WINDOWS",
            "RF_BUILD_DLL",                        -- 构建 DLL
            "NOMINMAX",
            "WIN32_LEAN_AND_MEAN"
        }

        -- DLL 生成后复制到 Sandbox 目录
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

    -- 分发行配置
    filter "configurations:Dist"
        defines { "RF_DIST" }
        optimize "On"
        runtime "Release"

-- ===========================================
--   Sandbox 测试项目（EXE）
-- ===========================================
project "Sandbox"
    location "Sandbox"
    kind "ConsoleApp"
    language "C++"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir    ("bin-int/" .. outputdir .. "/%{prj.name}")

    -- 源文件
    files {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.hpp",
        "%{prj.name}/src/**.cpp"
    }

    -- 头文件搜索路径
    includedirs {
        "Redflag/src",
        "Redflag/vendor/spdlog/include",
        "Redflag/vendor/fmt/include"
    }

    -- 链接 Redflag 引擎 DLL
    links { "Redflag" }

    -- Windows 配置
    filter "system:windows"
        systemversion "latest"
        cppdialect "C++17"
        characterset "Unicode"
        staticruntime "On"
        buildoptions { "/utf-8" }
        disablewarnings { "4251", "4828", "4996", "5030" }

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

    -- 分发行
    filter "configurations:Dist"
        defines { "RF_DIST" }
        optimize "On"
        runtime "Release"