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
IncludeDir["yaml_cpp"]="Redflag/vendor/yaml-cpp/include"
IncludeDir["ImGuizmo"]="Redflag/vendor/ImGuizmo"

group "Dependencies"  
    include "Redflag/vendor/GLFW"
    include "Redflag/vendor/Glad"
    include "Redflag/vendor/imgui"
    include "Redflag/vendor/yaml-cpp"
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
         "%{IncludeDir.yaml_cpp}", 
         "%{IncludeDir.ImGuizmo}", 
         "%{os.getenv('VULKAN_SDK')}/Include", -- [新增] Vulkan SDK 头文件
    }
    
     libdirs
    {
        "%{os.getenv('VULKAN_SDK')}/Lib", -- [新增] Vulkan SDK 库文件
    }

    links {
        "GLFW",
        "Glad",
        "ImGui",
        "opengl32.lib",
        "yaml-cpp",
        "shaderc_shared.lib",      -- [新增] shaderc 编译器
        "spirv-cross-core.lib",    -- [新增] SPIR-V 核心库
        "spirv-cross-glsl.lib"     -- [新增] SPIR-V 转 GLSL 工具
    }

    filter "files:**/vendor/ImGuizmo/**.cpp"
    flags { "NoPCH" }
    
    filter "system:windows"
        systemversion "latest"
        buildoptions { 
            "/utf-8",
        }
        defines {
            "RF_PLATFORM_WINDOWS",
            "GLFW_INCLUDE_NONE",
            "_CRT_SECURE_NO_WARNINGS",
            "YAML_CPP_STATIC_DEFINE",  -- 关键添加
        }

        -- [新增] 复制 shaderc_shared.dll 到输出目录
        postbuildcommands
        {
            "{COPY} %{cfg.buildtarget.relpath} ../bin/" .. outputdir .. "/RedflagForge",
            "{COPY} %{os.getenv('VULKAN_SDK')}/Bin/shaderc_shared.dll %{cfg.targetdir}"
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





project "RedflagForge"
    location "RedflagForge"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    staticruntime "off"
    
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


