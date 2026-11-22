@echo off
setlocal
if not defined VULKAN_SDK for /f "delims=" %%i in ('dir /b /ad /o-n C:\VulkanSDK 2^>nul') do if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\%%i"
cd /d "%~dp0.."
set "RF_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%RF_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RF_VS=%%i"
if not defined RF_VS exit /b 1
call "%RF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "bin-int\Tests" mkdir "bin-int\Tests"
cl /nologo /std:c++17 /EHsc /MDd /utf-8 /wd4828 /wd4005 /DRF_PLATFORM_WINDOWS /DGLFW_INCLUDE_NONE /DYAML_CPP_STATIC_DEFINE ^
 /I "%VULKAN_SDK%\Include" /I Redflag\vendor\yaml-cpp\include /I Redflag\vendor\stb_image /I Redflag\vendor\entt\include /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\spdlog\include ^
 /I Redflag\vendor\fmt\include /I Redflag\vendor\glm /I Redflag\vendor\Glad\include /I Redflag\vendor\GLFW\include ^
 /I RedflagForge\src /I Redflag\vendor\imgui tests\LightingPreviewEdits.cpp bin-int\Debug-windows-x86_64\RedflagForge\LightingPreview.obj /Fe:bin-int\Tests\LightingPreviewEdits.exe /Fo:bin-int\Tests\LightingPreviewEdits.obj ^
 /link bin\Debug-windows-x86_64\Redflag\Redflag.lib bin\Debug-windows-x86_64\GLFW\GLFW.lib ^
 bin\Debug-windows-x86_64\Glad\Glad.lib Redflag\vendor\assimp\bin\Debug-windows-x86_64\assimp\assimp.lib Redflag\vendor\Box2D\bin\Debug-windows-x86_64\Box2D\Box2D.lib Redflag\vendor\imgui\bin\Debug-windows-x86_64\ImGui\ImGui.lib Redflag\vendor\yaml-cpp\bin\Debug-windows-x86_64\yaml-cpp\yaml-cpp.lib /LIBPATH:"%VULKAN_SDK%\Lib" vulkan-1.lib opengl32.lib user32.lib gdi32.lib shell32.lib advapi32.lib ^
 /NODEFAULTLIB:LIBCMTD /NODEFAULTLIB:LIBCPMTD
if errorlevel 1 exit /b 1
set "PATH=%CD%\bin\Debug-windows-x86_64\RedflagForge;%PATH%"
set RF_VULKAN_VALIDATION=1
if /i "%~1"=="--skeleton" (
    bin-int\Tests\LightingPreviewEdits.exe tmp\skeleton-scene output\validation\character-animation\scene.csv --skeleton
) else if /i "%~1"=="--ragdoll" (
    bin-int\Tests\LightingPreviewEdits.exe tmp\ragdoll-scene output\validation\character-animation\physics-scene.csv --ragdoll
) else (
    bin-int\Tests\LightingPreviewEdits.exe tmp\lighting-preview-edits output\profiling\probe-materials\editor-edits.tsv
)
exit /b %errorlevel%
