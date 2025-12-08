project "Glad"
    kind "StaticLib"
    language "C"  -- glad.c 是 C 文件，应该用 C 编译器
    
    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")
    
    files {
        "src/glad.c",
        "include/glad/glad.h",
        "include/KHR/khrplatform.h"
    }
    
    includedirs {
        "include"
    }
    
    -- 关键：添加 C++ 兼容性定义
    defines {
        "GLAD_GLAPI_EXPORT",      -- 确保符号正确导出
        "GLAD_GLAPI_EXPORT_BUILD" -- 构建时导出
    }
    
    -- 在 Windows 上需要额外的配置
    filter "system:windows"
        systemversion "latest"
        staticruntime "on"
        defines {
            "_CRT_SECURE_NO_WARNINGS",  -- 禁用安全警告
            "GLAD_GLAPI_EXPORT=__declspec(dllexport)"  -- Windows 导出
        }
    
    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        runtime "Release"
        optimize "On"

    filter "configurations:Dist"
        runtime "Release"
        optimize "On"