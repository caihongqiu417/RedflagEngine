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
set "RF_OFF_LIB=bin\Debug-windows-x86_64\Redflag\Redflag.lib"
if defined RF_MASK_BASELINE_LIB set "RF_OFF_LIB=%RF_MASK_BASELINE_LIB%"
call :build MaskSupersamplingBaseline "%RF_OFF_LIB%"
if errorlevel 1 exit /b 1
call :build MaskSupersampling bin\Debug-windows-x86_64\Redflag\Redflag.lib /DRF_TEST_MASK_SSAA
if errorlevel 1 exit /b 1
if /i "%~1"=="--build-only" exit /b 0
set "PATH=%CD%\bin\Debug-windows-x86_64\RedflagForge;%PATH%"
set RF_VULKAN_VALIDATION=1
python tests\check_mask_supersampling.py
exit /b %errorlevel%

:build
cl /nologo /std:c++17 /EHsc /MDd /utf-8 /wd4828 /wd4005 /DRF_PLATFORM_WINDOWS /DGLFW_INCLUDE_NONE /DYAML_CPP_STATIC_DEFINE %3 ^
 /I "%VULKAN_SDK%\Include" /I Redflag\vendor\yaml-cpp\include /I Redflag\vendor\stb_image /I Redflag\vendor\entt\include /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\spdlog\include ^
 /I Redflag\vendor\fmt\include /I Redflag\vendor\glm /I Redflag\vendor\Glad\include /I Redflag\vendor\GLFW\include ^
 /I RedflagForge\src /I Redflag\vendor\imgui tests\MaskSupersampling.cpp /Fe:bin-int\Tests\%1.exe /Fo:bin-int\Tests\%1.obj ^
 /link "%~2" bin\Debug-windows-x86_64\GLFW\GLFW.lib ^
 bin\Debug-windows-x86_64\Glad\Glad.lib Redflag\vendor\assimp\bin\Debug-windows-x86_64\assimp\assimp.lib Redflag\vendor\Box2D\bin\Debug-windows-x86_64\Box2D\Box2D.lib Redflag\vendor\imgui\bin\Debug-windows-x86_64\ImGui\ImGui.lib Redflag\vendor\yaml-cpp\bin\Debug-windows-x86_64\yaml-cpp\yaml-cpp.lib /LIBPATH:"%VULKAN_SDK%\Lib" vulkan-1.lib opengl32.lib user32.lib gdi32.lib shell32.lib advapi32.lib ^
 /NODEFAULTLIB:LIBCMTD /NODEFAULTLIB:LIBCPMTD
exit /b %errorlevel%
