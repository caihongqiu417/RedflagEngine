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
IncludeDir["Imgui"] = "Redflag/vendor/imgui"
IncludeDir["glm"]    = "Redflag/vendor/glm" 

include "Redflag/vendor/GLFW"
include"Redflag/vendor/Glad"
include"Redflag/vendor/imgui"

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
        "%{prj.name}/src/**.cpp",
        "%{prj.name}/vendor/glm/glm/**.hpp",
        "%{prj.name}/vendor/glm/glm/**.inl"
    }
    
    includedirs {
        "%{prj.name}/src",
        "%{prj.name}/src/Redflag",
        "%{IncludeDir.spdlog}",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.fmt}",
        "%{IncludeDir.Glad}",
        "%{IncludeDir.Imgui}",
        "%{IncludeDir.glm}"
    }
    
    links {
        "GLFW",
        "Glad",
        "ImGui",
        "opengl32.lib",
    }
    
    filter "system:windows"
        systemversion "latest"
        -- ============================================
        -- 解决C4828编码警告的编译选项 - 开始
        -- ============================================
        buildoptions { 
            "/utf-8",  -- 强制使用UTF-8编码（包含source-charset和execution-charset）
        }
        -- ============================================
        -- 解决C4828编码警告的编译选项 - 结束
        -- ============================================
        defines {
            "RF_PLATFORM_WINDOWS",
            "GLFW_INCLUDE_NONE",
             -- ============================================
            -- 解决C4996不安全函数警告 - 开始
            -- ============================================
            "_CRT_SECURE_NO_WARNINGS",
            -- ============================================
            -- 解决C4996不安全函数警告 - 结束
            -- ============================================
        }
    
    filter "configurations:Debug"
        defines "RF_DEBUG"
        runtime "Debug"
        symbols "On"
        -- ============================================
        -- 禁用C4828警告（字符集无效警告）- 开始
        -- ============================================
        buildoptions { 
            "/wd4828",  -- 禁用C4828编码警告
            "/wd4005",  -- 禁用C4005宏重定义警告
        }
        -- ============================================
        -- 禁用C4828警告（字符集无效警告）- 结束
        -- ============================================
        
    filter "configurations:Release"
        defines "RF_RELEASE"
        runtime "Release"
        optimize "On"
        -- ============================================
        -- 禁用C4828警告（字符集无效警告）- 开始
        -- ============================================
        buildoptions { 
            "/wd4828",  -- 禁用C4828编码警告
            "/wd4005",  -- 禁用C4005宏重定义警告
        }
        -- ============================================
        -- 禁用C4828警告（字符集无效警告）- 结束
        -- ============================================
        
    filter "configurations:Dist"
        defines "RF_DIST"
        runtime "Release"
        optimize "On"
        -- ============================================
        -- 禁用C4828警告（字符集无效警告）- 开始
        -- ============================================
        buildoptions { 
            "/wd4828",  -- 禁用C4828编码警告
            "/wd4005",  -- 禁用C4005宏重定义警告
        }
        -- ============================================
        -- 禁用C4828警告（字符集无效警告）- 结束
        -- ============================================

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
        "%{prj.name}/src/**.cpp",
         "%{prj.name}/vendor/glm/glm/**.hpp",
        "%{prj.name}/vendor/glm/glm/**.inl"
    }
    
    includedirs {
        "Redflag/src",  -- 包含Redflag的头文件
        "Redflag/src/Redflag",  -- 包含Redflag的核心头文件
        "%{IncludeDir.spdlog}",
        "%{IncludeDir.fmt}",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.Glad}",
        "%{IncludeDir.glm}",
        "%{IncludeDir.Imgui}"
    }
    
    links {
        "Redflag",  -- 链接到Redflag静态库
        "GLFW",
        "Glad",
        "opengl32.lib",
    }
    
    filter "system:windows"
        systemversion "latest"
        -- ============================================
        -- 解决C4828编码警告的编译选项 - 开始
        -- ============================================
        buildoptions { 
            "/utf-8",  -- 强制使用UTF-8编码（包含source-charset和execution-charset）
        }
        -- ============================================
        -- 解决C4828编码警告的编译选项 - 结束
        -- ============================================
        defines {
            "RF_PLATFORM_WINDOWS",
             -- ============================================
            -- 解决C4996不安全函数警告 - 开始
            -- ============================================
            "_CRT_SECURE_NO_WARNINGS",
            -- ============================================
            -- 解决C4996不安全函数警告 - 结束
            -- ============================================
        }
    
    filter "configurations:Debug"
        defines "RF_DEBUG"
        runtime "Debug"
        symbols "On"
        -- ============================================
        -- 禁用C4828警告（字符集无效警告）- 开始
        -- ============================================
        buildoptions { 
            "/wd4828",  -- 禁用C4828编码警告
            "/wd4005",  -- 禁用C4005宏重定义警告
        }
        -- ============================================
        -- 禁用C4828警告（字符集无效警告）- 结束
        -- ============================================
        
    filter "configurations:Release"
        defines "RF_RELEASE"
        runtime "Release"
        optimize "On"
        -- ============================================
        -- 禁用C4828警告（字符集无效警告）- 开始
        -- ============================================
        buildoptions { 
            "/wd4828",  -- 禁用C4828编码警告
            "/wd4005",  -- 禁用C4005宏重定义警告
        }
        -- ============================================
        -- 禁用C4828警告（字符集无效警告）- 结束
        -- ============================================
        
    filter "configurations:Dist"
        defines "RF_DIST"
        runtime "Release"
        optimize "On"
        -- ============================================
        -- 禁用C4828警告（字符集无效警告）- 开始
        -- ============================================
        buildoptions { 
            "/wd4828",  -- 禁用C4828编码警告
            "/wd4005",  -- 禁用C4005宏重定义警告
        }
        -- ============================================
        -- 禁用C4828警告（字符集无效警告）- 结束
        -- ============================================