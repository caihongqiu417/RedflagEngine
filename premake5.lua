workspace "Redflag"
    architecture "x86_64"
    startproject "RedflagForge"

    configurations 
    { 
        "Debug", "Release", "Dist"
    }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IncludeDir = {}
IncludeDir["GLFW"] = "Redflag/vendor/GLFW/include"
IncludeDir["fmt"] = "Redflag/vendor/fmt/include"
IncludeDir["Glad"] = "Redflag/vendor/Glad/include"
IncludeDir["Imgui"] = "Redflag/vendor/imgui"
IncludeDir["glm"]    = "Redflag/vendor/glm" 
IncludeDir["stb_image"]    = "Redflag/vendor/stb_image"
IncludeDir["spdlog"] = "Redflag/vendor/spdlog/include"  
IncludeDir["entt"]="Redflag/vendor/entt/include"

group "Dependencies"  
    include "Redflag/vendor/GLFW"
    include "Redflag/vendor/Glad"
    include "Redflag/vendor/imgui"

group ""

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
        "%{prj.name}/vendor/stb_image/**.h",
        "%{prj.name}/vendor/stb_image/**.cpp",
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
        "%{IncludeDir.glm}",
        "%{IncludeDir.stb_image}",
        "%{IncludeDir.entt}",
    }
    
    links {
        "GLFW",
        "Glad",
        "ImGui",
        "opengl32.lib",
    }
    
    filter "system:windows"
        systemversion "latest"
        buildoptions { 
            "/utf-8",
        }
        defines {
            "RF_PLATFORM_WINDOWS",
            "GLFW_INCLUDE_NONE",
            "_CRT_SECURE_NO_WARNINGS",
        }
    
    filter "configurations:Debug"
        defines "RF_DEBUG"
        runtime "Debug"
        symbols "On"
        buildoptions { 
            "/wd4828",
            "/wd4005",
        }
        
    filter "configurations:Release"
        defines "RF_RELEASE"
        runtime "Release"
        optimize "On"
        buildoptions { 
            "/wd4828",
            "/wd4005",
        }
        
    filter "configurations:Dist"
        defines "RF_DIST"
        runtime "Release"
        optimize "On"
        buildoptions { 
            "/wd4828",
            "/wd4005",
        }

project "Sandbox"
    location "Sandbox"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    staticruntime "on"
    
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    
    flags { "NoPCH" }
    
    files {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.cpp",
    }
    
    includedirs {
        "Redflag/src",
        "Redflag/src/Redflag",  
        "%{IncludeDir.spdlog}",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.Glad}",
        "%{IncludeDir.Imgui}",
        "%{IncludeDir.glm}",
        "%{IncludeDir.fmt}",
        "%{IncludeDir.entt}",
    }
    
    links {
        "Redflag",
        "GLFW",
        "Glad",
        "opengl32.lib",
    }
    
    filter "system:windows"
        systemversion "latest"
        buildoptions { 
            "/utf-8",
        }
        defines {
            "RF_PLATFORM_WINDOWS",
            "_CRT_SECURE_NO_WARNINGS",
        }
    
    filter "configurations:Debug"
        defines "RF_DEBUG"
        runtime "Debug"
        symbols "On"
        buildoptions { 
            "/wd4828",
            "/wd4005",
        }
        
    filter "configurations:Release"
        defines "RF_RELEASE"
        runtime "Release"
        optimize "On"
        buildoptions { 
            "/wd4828",
            "/wd4005",
        }
        
    filter "configurations:Dist"
        defines "RF_DIST"
        runtime "Release"
        optimize "On"
        buildoptions { 
            "/wd4828",
            "/wd4005",
        }





project "RedflagForge"
    location "RedflagForge"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    staticruntime "on"
    
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    
     -- 明确禁用预编译头
    flags { "NoPCH" }
    
    files {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.cpp",
    }
    
    includedirs {
        "Redflag/src",
        "Redflag/src/Redflag",  
        "%{IncludeDir.spdlog}",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.Glad}",
        "%{IncludeDir.Imgui}",
        "%{IncludeDir.glm}",
        "%{IncludeDir.fmt}",
    }
    
    links {
        "Redflag",
        "GLFW",
        "Glad",
        "opengl32.lib",
    }
    
    filter "system:windows"
        systemversion "latest"
        buildoptions { 
            "/utf-8",
        }
        defines {
            "RF_PLATFORM_WINDOWS",
            "_CRT_SECURE_NO_WARNINGS",
        }
    
    filter "configurations:Debug"
        defines "RF_DEBUG"
        runtime "Debug"
        symbols "On"
        buildoptions { 
            "/wd4828",
            "/wd4005",
        }
        
    filter "configurations:Release"
        defines "RF_RELEASE"
        runtime "Release"
        optimize "On"
        buildoptions { 
            "/wd4828",
            "/wd4005",
        }
        
    filter "configurations:Dist"
        defines "RF_DIST"
        runtime "Release"
        optimize "On"
        buildoptions { 
            "/wd4828",
            "/wd4005",
        }


