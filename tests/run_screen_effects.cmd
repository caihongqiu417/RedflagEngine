@echo off
setlocal
cd /d "%~dp0.."
if not defined VULKAN_SDK for /f "delims=" %%i in ('dir /b /ad /o-n C:\VulkanSDK 2^>nul') do if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\%%i"
if not exist "%VULKAN_SDK%\Include\vulkan\vulkan.h" (
    echo Install the Vulkan SDK and set VULKAN_SDK before running the RHI tests.
    exit /b 1
)
set "RF_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%RF_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RF_VS=%%i"
if not defined RF_VS exit /b 1
call "%RF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "bin-int\Tests" mkdir "bin-int\Tests"
cl /nologo /std:c++17 /EHsc /MDd /utf-8 /wd4828 /wd4005 /DRF_PLATFORM_WINDOWS /DGLFW_INCLUDE_NONE ^
 /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\spdlog\include ^
 /I "%VULKAN_SDK%\Include" /I Redflag\vendor\fmt\include /I Redflag\vendor\glm /I Redflag\vendor\Glad\include /I Redflag\vendor\GLFW\include ^
 tests\ScreenEffectsTests.cpp /Fe:bin-int\Tests\ScreenEffectsTests.exe /Fo:bin-int\Tests\ScreenEffectsTests.obj ^
 /link bin\Debug-windows-x86_64\Redflag\Redflag.lib bin\Debug-windows-x86_64\GLFW\GLFW.lib ^
 bin\Debug-windows-x86_64\Glad\Glad.lib /LIBPATH:"%VULKAN_SDK%\Lib" Redflag\vendor\assimp\bin\Debug-windows-x86_64\assimp\assimp.lib advapi32.lib vulkan-1.lib opengl32.lib user32.lib gdi32.lib shell32.lib ^
 /NODEFAULTLIB:LIBCMTD /NODEFAULTLIB:LIBCPMTD
if errorlevel 1 exit /b 1
set RF_VULKAN_VALIDATION=1
set "PATH=%CD%\bin\Debug-windows-x86_64\RedflagForge;%PATH%"
rem Run from project root
set "RF_SCREEN_OUTPUT=tmp\screen-effects"
if /i "%~1"=="--directional" set "RF_SCREEN_OUTPUT=output\validation\directional"
if /i "%~1"=="--gtao-camera" set "RF_SCREEN_OUTPUT=tmp\gtao-camera"
bin-int\Tests\ScreenEffectsTests.exe "RedflagForge\assets\models\reference\Sponza\Sponza.gltf" "%RF_SCREEN_OUTPUT%" %*
if errorlevel 1 exit /b 1
if /i "%~1"=="--capture" exit /b 0
if /i "%~1"=="--point-materials" exit /b 0
if /i "%~1"=="--directional" exit /b 0
if /i "%~1"=="--gtao-camera" exit /b 0
python tests\screen_effects_report.py
if errorlevel 1 exit /b 1
python tests\temporal_quality.py
exit /b %errorlevel%

