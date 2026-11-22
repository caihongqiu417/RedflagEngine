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
set "rasterTest=TAACameraTests"
if defined RF_WIRE_RASTER set "rasterTest=WireRasterTests"
if defined RF_CASCADE_TEST (
    set "rasterTest=CascadeShadowTests"
    "%VULKAN_SDK%\Bin\glslc.exe" --target-env=vulkan1.1 -O -I Redflag\src\Redflag\Renderer\GI\Shaders tests\fixtures\ShadowReceiver.comp -o bin-int\Tests\ShadowReceiver.spv
    if errorlevel 1 exit /b 1
)
cl /nologo /std:c++17 /EHsc /MDd /utf-8 /wd4828 /wd4005 /DRF_PLATFORM_WINDOWS /DGLFW_INCLUDE_NONE ^
 /I Redflag\vendor\stb_image /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\spdlog\include ^
 /I "%VULKAN_SDK%\Include" /I Redflag\vendor\fmt\include /I Redflag\vendor\glm /I Redflag\vendor\Glad\include /I Redflag\vendor\GLFW\include ^
 tests\%rasterTest%.cpp /Fe:bin-int\Tests\%rasterTest%.exe /Fo:bin-int\Tests\%rasterTest%.obj ^
 /link bin\Debug-windows-x86_64\Redflag\Redflag.lib bin\Debug-windows-x86_64\GLFW\GLFW.lib ^
 bin\Debug-windows-x86_64\Glad\Glad.lib /LIBPATH:"%VULKAN_SDK%\Lib" Redflag\vendor\assimp\bin\Debug-windows-x86_64\assimp\assimp.lib advapi32.lib vulkan-1.lib opengl32.lib user32.lib gdi32.lib shell32.lib ^
 /NODEFAULTLIB:LIBCMTD /NODEFAULTLIB:LIBCPMTD
if errorlevel 1 exit /b 1
set RF_VULKAN_VALIDATION=1
set "PATH=%CD%\bin\Debug-windows-x86_64\RedflagForge;%PATH%"
if not "%~2"=="" if not "%~2"=="--profile" (
    set "VK_ADD_LAYER_PATH=%~dp2"
    set "VK_INSTANCE_LAYERS=VK_LAYER_RENDERDOC_Capture"
)
bin-int\Tests\%rasterTest%.exe %*
exit /b %errorlevel%
