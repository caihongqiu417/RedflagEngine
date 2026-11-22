workspace "Redflag"
    architecture "x86_64"
    startproject "RedflagForge"

    configurations 
    { 
        "Debug", "Release", "Dist"
    }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

-- 本地已有的 Mono 保持原路径。全新检出从 Windows 的 Mono x64 SDK 取得导入库与 DLL。
local monoSDK = os.getenv("REDFLAG_MONO_SDK")
local monoLib = "Redflag/vendor/mono/lib/Release"
local monoDLL = "../" .. monoLib .. "/mono-2.0-sgen.dll"
if monoSDK or not os.isfile(monoLib .. "/mono-2.0-sgen.lib") then
    monoSDK = monoSDK or path.join(os.getenv("ProgramW6432") or "C:/Program Files", "Mono")
    monoLib = path.join(monoSDK, "lib")
    monoDLL = path.join(monoSDK, "bin/mono-2.0-sgen.dll")
    assert(os.isfile(monoLib .. "/mono-2.0-sgen.lib") and os.isfile(monoDLL),
        "Install the Windows x64 Mono 6.12 SDK or set REDFLAG_MONO_SDK to its installation directory.")
end

-- 修正只应用到编译副本，重新生成工程时不会改动第三方工作目录。
local function dependencyCopy(name, file)
    local folder = "bin-int/dependencies/" .. name
    local dest = folder .. "/" .. file
    os.mkdir(path.getdirectory(dest))
    assert(os.copyfile("Redflag/vendor/" .. name .. "/" .. file, dest), "Could not copy " .. file)
    return dest
end

local function patchCopy(name, patch)
    local cmd = 'git apply --recount --directory="bin-int/dependencies/' .. name .. '" "patches/' .. patch .. '.patch"'
    local ok = os.execute(cmd)
    assert(ok == true or ok == 0, "Could not apply " .. patch .. " to the build copy")
end


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
    project "GLFW"
        targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
        objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")
        filter "system:windows"
            includedirs { "Redflag/vendor/GLFW/src", "Redflag/vendor/GLFW/include" }
        filter {}

    include "Redflag/vendor/Glad"
    include "Redflag/vendor/imgui"
    project "ImGui"
        local copy = dependencyCopy("imgui", "imgui.cpp")
        local contents = assert(io.readfile(copy))
        local active = contents:match("void ImGui::SetActiveID%b()%s*(%b{})")
        -- 公开版本把销毁窗口布局的循环误放在切换控件焦点时。恢复到 ImGuiWindow 的析构函数。
        if active and active:find("i < DC.Layouts.Data.Size", 1, true) then
            patchCopy("imgui", "imgui-layout-lifetime")
        end
        removefiles { "Redflag/vendor/imgui/imgui.cpp" }
        files { copy }
        includedirs { "Redflag/vendor/imgui" }
        filter "system:windows"
            buildoptions { "/utf-8" }
        filter {}

    include "Redflag/vendor/yaml-cpp"
    project "yaml-cpp"
        filter "system:windows"
            buildoptions { "/utf-8" }
        filter {}

    include "Redflag/vendor/assimp"
    project "assimp"
        -- 保留 FBX 导入器及其自带的 zlib，运行库与 Redflag 的 /MD、/MDd 一致。
        staticruntime "off"
        removedefines { "ASSIMP_BUILD_NO_OWN_ZLIB", "ASSIMP_BUILD_NO_FBX_IMPORTER", "ASSIMP_BUILD_NO_GENFACENORMALS_PROCESS" }
        files { "Redflag/vendor/assimp/code/FBX*.cpp", "Redflag/vendor/assimp/contrib/zlib/*.c",
                "Redflag/vendor/assimp/code/GenFaceNormalsProcess.cpp" }
        removefiles { "Redflag/vendor/assimp/code/FBXExport*.cpp", "Redflag/vendor/assimp/code/glTF2Importer.cpp" }

        local importer = dependencyCopy("assimp", "code/glTF2Importer.cpp")
        local material = dependencyCopy("assimp", "code/glTF2Asset.inl")
        dependencyCopy("assimp", "code/glTF2Asset.h")
        if not assert(io.readfile(material)):find("KHR_materials_emissive_strength", 1, true) then
            patchCopy("assimp", "assimp-gltf-emissive-strength")
        end
        if not assert(io.readfile(importer)):find("const int twoSided = mat.doubleSided", 1, true) then
            patchCopy("assimp", "assimp-gltf-double-sided")
        end
        includedirs { "Redflag/vendor/assimp/code" }
        files { importer }
group ""

group "Core"
    include "Redflag-ScriptCore"
    include "SandboxProject"

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
        "vulkan-1.lib",
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
        libdirs { monoLib }

        -- [Debug] 链接带 'd' 的库
        links {
            "shaderc_sharedd.lib",
            "spirv-cross-cored.lib",
            "spirv-cross-glsld.lib",
            "mono-2.0-sgen.lib",
        }

        -- [Debug] 复制 DLL 和 Mono 托管程序集目录
        postbuildcommands {
            "{COPY} %{os.getenv('VULKAN_SDK')}/Bin/shaderc_sharedd.dll ../bin/" .. outputdir .. "/RedflagForge",
            "{COPY} %{os.getenv('VULKAN_SDK')}/Bin/shaderc_sharedd.dll ../bin/" .. outputdir .. "/Sandbox",
            '{COPY} "' .. monoDLL .. '" ../bin/' .. outputdir .. "/RedflagForge",
            '{COPY} "' .. monoDLL .. '" ../bin/' .. outputdir .. "/Sandbox",
            "{COPY} ../RedflagForge/mono ../bin/" .. outputdir .. "/RedflagForge/mono",
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

        libdirs { monoLib }
        
        -- [Release] 链接不带 'd' 的库
        links {
            "shaderc_shared.lib",
            "spirv-cross-core.lib",
            "spirv-cross-glsl.lib",
            "mono-2.0-sgen.lib",
        }

        -- [Release] 复制不带 'd' 的 DLL 和 Mono 托管程序集目录
        postbuildcommands {
            "{COPY} %{os.getenv('VULKAN_SDK')}/Bin/shaderc_shared.dll ../bin/" .. outputdir .. "/RedflagForge",
            "{COPY} %{os.getenv('VULKAN_SDK')}/Bin/shaderc_shared.dll ../bin/" .. outputdir .. "/Sandbox",
            '{COPY} "' .. monoDLL .. '" ../bin/' .. outputdir .. "/RedflagForge",
            '{COPY} "' .. monoDLL .. '" ../bin/' .. outputdir .. "/Sandbox",
            "{COPY} ../RedflagForge/mono ../bin/" .. outputdir .. "/RedflagForge/mono",
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

        libdirs { monoLib }
        
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
            '{COPY} "' .. monoDLL .. '" ../bin/' .. outputdir .. "/RedflagForge",
            '{COPY} "' .. monoDLL .. '" ../bin/' .. outputdir .. "/Sandbox",
            "{COPY} ../RedflagForge/mono ../bin/" .. outputdir .. "/RedflagForge/mono",
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
        -- spirv-cross-cored.lib 用 /MTd 编译，嵌入了 LIBCMTD + LIBCPMTD 两个静态 CRT 默认库，
        -- 与项目 /MDd (msvcrtd + msvcprtd) 冲突，导致 msvcp140d.lib 中新增的
        -- __std_find_last_trivial_1 等符号无法解析，需同时屏蔽两者。
        linkoptions { "/NODEFAULTLIB:LIBCMTD", "/NODEFAULTLIB:LIBCPMTD" }
        
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
    dependson { "Redflag-ScriptCore", "SandboxProject" }
    
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    
    flags { "NoPCH" }
    defines { "RF_FORGE" }
    
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
        "%{IncludeDir.ImGuizmo}",
        "%{IncludeDir.yaml_cpp}",
        "%{os.getenv('VULKAN_SDK')}/Include"
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
        -- spirv-cross-cored.lib 用 /MTd 编译，嵌入了 LIBCMTD + LIBCPMTD 两个静态 CRT 默认库，
        -- 与项目 /MDd (msvcrtd + msvcprtd) 冲突，导致 msvcp140d.lib 中新增的
        -- __std_find_last_trivial_1 等符号无法解析，需同时屏蔽两者。
        linkoptions { "/NODEFAULTLIB:LIBCMTD", "/NODEFAULTLIB:LIBCPMTD" }
        
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
