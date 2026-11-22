@echo off
setlocal
cd /d "%~dp0.."
if not defined VULKAN_SDK for /f "delims=" %%i in ('dir /b /ad /o-n C:\VulkanSDK 2^>nul') do if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\%%i"
set "SDK=bin-int\dependencies\PhysX"
if not exist "%SDK%\lib\debug\PhysX_static_64.lib" (
    echo Run scripts\BuildPhysX.ps1 -Configuration Debug first.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RF_VS=%%i"
call "%RF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "OUT=bin-int\Tests\Character"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++17 /EHsc /MDd /utf-8 /wd4828 /wd4005 /DRF_PLATFORM_WINDOWS /DGLFW_INCLUDE_NONE /DPX_DEBUG=1 /DPX_CHECKED=1 ^
 /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\spdlog\include /I Redflag\vendor\glm /I "%SDK%\include" ^
 /I "%VULKAN_SDK%\Include" /I Redflag\vendor\GLFW\include /I Redflag\vendor\Glad\include /I Redflag\vendor\imgui ^
 tests\CharacterAnimationTests.cpp examples\CharacterAnimation\*.cpp ^
 /Fe:"%OUT%\CharacterAnimationTests.exe" /Fo:"%OUT%\\" /link ^
 bin\Debug-windows-x86_64\Redflag\Redflag.lib bin\Debug-windows-x86_64\GLFW\GLFW.lib bin\Debug-windows-x86_64\Glad\Glad.lib ^
 Redflag\vendor\imgui\bin\Debug-windows-x86_64\ImGui\ImGui.lib ^
 /LIBPATH:"%SDK%\lib\debug" PhysX_static_64.lib PhysXExtensions_static_64.lib PhysXCommon_static_64.lib PhysXFoundation_static_64.lib PhysXPvdSDK_static_64.lib PhysXCooking_static_64.lib ^
 /LIBPATH:"%VULKAN_SDK%\Lib" vulkan-1.lib opengl32.lib user32.lib gdi32.lib shell32.lib /NODEFAULTLIB:LIBCMTD /NODEFAULTLIB:LIBCPMTD
if errorlevel 1 exit /b 1
set RF_VULKAN_VALIDATION=1
cd RedflagForge
..\%OUT%\CharacterAnimationTests.exe %*
exit /b %errorlevel%
