workspace "Redflag"
    architecture "x64"
    configurations { "Debug", "Release", "Dist" }
    startproject "Sandbox"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IncludeDir = {}
IncludeDir["GLFW"] = "Redflag/vendor/GLFW/include"
IncludeDir["spdlog"] = "Redflag/vendor/spdlog/include"
IncludeDir["fmt"] = "Redflag/vendor/fmt/include"
IncludeDir["Glad"] = "Redflag/vendor/Glad/include"

include "Redflag/vendor/GLFW"
include"Redflag/vendor/Glad"

project "Redflag"
    location "Redflag"
    kind "StaticLib"
    language "C++"
    
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    
    -- Redflag 创建自己的预编译头
    pchheader "rfpch.h"
    pchsource "Redflag/src/Redflag/rfpch.cpp"
    
    cppdialect "C++17"
    staticruntime "on"
    
    files {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.cpp"
    }
    
    includedirs {
        "%{prj.name}/src",
        "%{prj.name}/src/Redflag",
        "%{IncludeDir.spdlog}",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.fmt}",
        "%{IncludeDir.Glad}"
    }
    
    links {
        "GLFW",
        "Glad",
        "opengl32.lib",
    }
    
    filter "system:windows"
        systemversion "latest"
        buildoptions { "/utf-8" }
        defines {
            "RF_PLATFORM_WINDOWS",
            "GLFW_INCLUDE_NONE"
        }
    
    filter "configurations:Debug"
        defines "RF_DEBUG"
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        defines "RF_RELEASE"
        runtime "Release"
        optimize "On"

    filter "configurations:Dist"
        defines "RF_DIST"
        runtime "Release"
        optimize "On"

project "Sandbox"
    location "Sandbox"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    staticruntime "on"
    
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    
    -- Sandbox 创建自己的预编译头（如果需要的话）
    -- 或者完全禁用预编译头
    flags { "NoPCH" }  -- 禁用预编译头，因为Sandbox很小
    
    files {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.cpp"
    }
    
    includedirs {
        "Redflag/src",  -- 包含Redflag的头文件
        "Redflag/src/Redflag",  -- 包含Redflag的核心头文件
        "%{IncludeDir.spdlog}",
        "%{IncludeDir.fmt}",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.Glad}"
    }
    
    links {
        "Redflag",  -- 链接到Redflag静态库
        "GLFW",
        "Glad",
        "opengl32.lib",
    }
    
    filter "system:windows"
        systemversion "latest"
        buildoptions { "/utf-8" }
        defines {
            "RF_PLATFORM_WINDOWS"
        }
    
    filter "configurations:Debug"
        defines "RF_DEBUG"
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        defines "RF_RELEASE"
        runtime "Release"
        optimize "On"

    filter "configurations:Dist"
        defines "RF_DIST"
        runtime "Release"
        optimize "On"