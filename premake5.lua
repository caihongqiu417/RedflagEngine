workspace "Redflag"
    architecture "x86_64"
    startproject "RedflagForge"

    configurations 
    { 
        "Debug", "Release", "Dist"
    }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

--[[
    第三方库头文件路径映射表
    集中管理所有依赖库的 Include 路径，方便维护
--]]
IncludeDir = {}
IncludeDir["GLFW"] = "Redflag/vendor/GLFW/include"
IncludeDir["fmt"] = "Redflag/vendor/fmt/include"
IncludeDir["Box2D"]= "Redflag/vendor/Box2D/include"
IncludeDir["Glad"] = "Redflag/vendor/Glad/include"
IncludeDir["Imgui"] = "Redflag/vendor/imgui"
IncludeDir["glm"]    = "Redflag/vendor/glm" 
IncludeDir["stb_image"]    = "Redflag/vendor/stb_image"
IncludeDir["spdlog"] = "Redflag/vendor/spdlog/include"  
IncludeDir["entt"]="Redflag/vendor/entt/include"
IncludeDir["yaml_cpp"]="Redflag/vendor/yaml-cpp/include"
IncludeDir["ImGuizmo"]="Redflag/vendor/ImGuizmo"
IncludeDir["assimp"]  = "Redflag/vendor/assimp/include"
IncludeDir["mono"]    = "Redflag/vendor/mono/include"
IncludeDir["tinyexr"]  = "Redflag/vendor/tinyexr"
IncludeDir["miniz"]    = "Redflag/vendor/tinyexr/deps/miniz"

--实际编译
group "Dependencies"  
    include "Redflag/vendor/Box2D"
    include "Redflag/vendor/GLFW"
    include "Redflag/vendor/Glad"
    include "Redflag/vendor/imgui"
    include "Redflag/vendor/yaml-cpp"
    include "Redflag/vendor/assimp"
group ""

group "Core"
    include "Redflag-ScriptCore"

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
    staticruntime "off"
    
    files {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.cpp",
        "%{prj.name}/vendor/stb_image/**.h",
        "%{prj.name}/vendor/stb_image/**.cpp",
        "%{prj.name}/vendor/glm/glm/**.hpp",
        "%{prj.name}/vendor/glm/glm/**.inl",
        "%{prj.name}/vendor/ImGuizmo/ImGuizmo.h",
        "%{prj.name}/vendor/ImGuizmo/ImGuizmo.cpp",
        "%{prj.name}/vendor/tinyexr/deps/miniz/miniz.c",
    }
    
    --告诉头文件要去这些地方找
    includedirs {
        "%{prj.name}/src",
        "%{prj.name}/src/Redflag",
        "%{IncludeDir.Box2D}",
        "%{IncludeDir.spdlog}", 
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.fmt}",
        "%{IncludeDir.Glad}",
        "%{IncludeDir.Imgui}",
        "%{IncludeDir.glm}",
        "%{IncludeDir.stb_image}",
        "%{IncludeDir.entt}",
        "%{IncludeDir.yaml_cpp}", 
        "%{IncludeDir.ImGuizmo}",
        "%{IncludeDir.assimp}",
        "%{IncludeDir.mono}",
        "%{IncludeDir.tinyexr}",
        "%{IncludeDir.miniz}",
        "%{os.getenv('VULKAN_SDK')}/Include", 
    }
    
    libdirs
    {
        "%{os.getenv('VULKAN_SDK')}/Lib", 
    }

    -- 基础链接库 (所有配置共用)
    links {
        "Box2D",
        "GLFW",
        "Glad",
        "ImGui",
        "opengl32.lib",
        "yaml-cpp",
        "assimp",
    }

    filter "files:**/vendor/ImGuizmo/**.cpp"
        flags { "NoPCH" }

    filter "files:**/vendor/tinyexr/deps/miniz/miniz.c"
        flags { "NoPCH" }
        compileas "C"
    
    filter "system:windows"
        systemversion "latest"
        buildoptions { 
            "/utf-8",
        }
        defines {
            "RF_PLATFORM_WINDOWS",
            "RF_BUILD_DLL",
            "GLFW_INCLUDE_NONE",
            "_CRT_SECURE_NO_WARNINGS",
            "YAML_CPP_STATIC_DEFINE",
        }
        
        -- 基础 Post-build (所有配置共用)
        postbuildcommands
        {
            "{COPY} %{cfg.buildtarget.relpath} ../bin/" .. outputdir .. "/RedflagForge",
        }
    
    -- Debug 配置
    filter "configurations:Debug"
        defines "RF_DEBUG"
        runtime "Debug"
        symbols "On"
        buildoptions { 
            "/wd4828",
            "/wd4005",
        }

        libdirs { "Redflag/vendor/mono/lib/Debug" }
        
        -- [Debug] 链接带 'd' 的库
        links {
            "shaderc_sharedd.lib",
            "spirv-cross-cored.lib",
            "spirv-cross-glsld.lib",
            "mono-2.0-sgen.lib",
        }
        
        -- [修改后] 显式复制到 RedflagForge 和 Sandbox 目录
        postbuildcommands {
            "{COPY} %{os.getenv('VULKAN_SDK')}/Bin/shaderc_sharedd.dll ../bin/" .. outputdir .. "/RedflagForge",
            "{COPY} %{os.getenv('VULKAN_SDK')}/Bin/shaderc_sharedd.dll ../bin/" .. outputdir .. "/Sandbox",
            "{COPY} ../Redflag/vendor/mono/lib/Debug/mono-2.0-sgen.dll ../bin/" .. outputdir .. "/RedflagForge",
            "{COPY} ../Redflag/vendor/mono/lib/Debug/mono-2.0-sgen.dll ../bin/" .. outputdir .. "/Sandbox",
        }
        
    -- Release 配置
    filter "configurations:Release"
        defines "RF_RELEASE"
        runtime "Release"
        optimize "On"
        buildoptions { 
            "/wd4828",
            "/wd4005",
        }

        libdirs { "Redflag/vendor/mono/lib/Release" }
        
        -- [Release] 链接不带 'd' 的库
        links {
            "shaderc_shared.lib",
            "spirv-cross-core.lib",
            "spirv-cross-glsl.lib",
            "mono-2.0-sgen.lib",
        }

        -- [Release] 复制不带 'd' 的 DLL
       -- [修改后]
        postbuildcommands {
            "{COPY} %{os.getenv('VULKAN_SDK')}/Bin/shaderc_shared.dll ../bin/" .. outputdir .. "/RedflagForge",
            "{COPY} %{os.getenv('VULKAN_SDK')}/Bin/shaderc_shared.dll ../bin/" .. outputdir .. "/Sandbox",
            "{COPY} ../Redflag/vendor/mono/lib/Release/mono-2.0-sgen.dll ../bin/" .. outputdir .. "/RedflagForge",
            "{COPY} ../Redflag/vendor/mono/lib/Release/mono-2.0-sgen.dll ../bin/" .. outputdir .. "/Sandbox",
        }
        
    -- Dist 配置
    filter "configurations:Dist"
        defines "RF_DIST"
        runtime "Release"
        optimize "On"
        buildoptions { 
            "/wd4828",
            "/wd4005",
        }

        libdirs { "Redflag/vendor/mono/lib/Release" }
        
        -- [Dist] 链接不带 'd' 的库
        links {
            "shaderc_shared.lib",
            "spirv-cross-core.lib",
            "spirv-cross-glsl.lib",
            "mono-2.0-sgen.lib",
        }

        -- [修改后]
        postbuildcommands {
            "{COPY} %{os.getenv('VULKAN_SDK')}/Bin/shaderc_shared.dll ../bin/" .. outputdir .. "/RedflagForge",
            "{COPY} %{os.getenv('VULKAN_SDK')}/Bin/shaderc_shared.dll ../bin/" .. outputdir .. "/Sandbox",
            "{COPY} ../Redflag/vendor/mono/lib/Release/mono-2.0-sgen.dll ../bin/" .. outputdir .. "/RedflagForge",
            "{COPY} ../Redflag/vendor/mono/lib/Release/mono-2.0-sgen.dll ../bin/" .. outputdir .. "/Sandbox",
        }

group "Misc"

project "Sandbox"
    location "Sandbox"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    staticruntime "off"
    
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
            "YAML_CPP_STATIC_DEFINE",
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

group "Tools"

project "RedflagForge"
    location "RedflagForge"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    staticruntime "off"
    
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
        "%{IncludeDir.ImGuizmo}"
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
            "YAML_CPP_STATIC_DEFINE",
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