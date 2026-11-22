@echo off
setlocal
cd /d "%~dp0.."
if not defined RF_SERVER_DIR set "RF_SERVER_DIR=%CD%\..\REDFServer"
if not exist "%RF_SERVER_DIR%\world_server.cpp" (
    echo Set RF_SERVER_DIR to the checkout containing world_server.cpp.
    exit /b 1
)
if not defined VULKAN_SDK for /f "delims=" %%i in ('dir /b /ad /o-n C:\VulkanSDK 2^>nul') do if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\%%i"
set "RF_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%RF_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RF_VS=%%i"
call "%RF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist bin-int\Tests mkdir bin-int\Tests
cl /nologo /EHsc /std:c++17 /MD /utf-8 "%RF_SERVER_DIR%\world_server.cpp" /Fo:bin-int\Tests\world_server.obj /Fe:bin-int\Tests\world_server.exe || exit /b 1
set "RF_PHYSX=bin-int\dependencies\PhysX\lib\debug"
cl /nologo /std:c++17 /EHsc /MDd /utf-8 /wd4828 /wd4005 /DRF_PLATFORM_WINDOWS /DGLFW_INCLUDE_NONE ^
 /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\entt\include /I Redflag\vendor\glm /I Redflag\vendor\spdlog\include ^
 tests\WorldSessionTests.cpp /Fo:bin-int\Tests\WorldSessionTests.obj /Fe:bin-int\Tests\WorldSessionTests.exe ^
 /link bin\Debug-windows-x86_64\Redflag\Redflag.lib bin\Debug-windows-x86_64\GLFW\GLFW.lib bin\Debug-windows-x86_64\Glad\Glad.lib ^
 Redflag\vendor\assimp\bin\Debug-windows-x86_64\assimp\assimp.lib Redflag\vendor\Box2D\bin\Debug-windows-x86_64\Box2D\Box2D.lib ^
 Redflag\vendor\imgui\bin\Debug-windows-x86_64\ImGui\ImGui.lib Redflag\vendor\yaml-cpp\bin\Debug-windows-x86_64\yaml-cpp\yaml-cpp.lib ^
 /LIBPATH:"%RF_PHYSX%" PhysX_static_64.lib PhysXFoundation_static_64.lib PhysXCommon_static_64.lib PhysXExtensions_static_64.lib PhysXPvdSDK_static_64.lib ^
 /LIBPATH:"%VULKAN_SDK%\Lib" vulkan-1.lib opengl32.lib user32.lib gdi32.lib shell32.lib advapi32.lib /NODEFAULTLIB:LIBCMTD /NODEFAULTLIB:LIBCPMTD
if errorlevel 1 exit /b 1
set "PATH=%CD%\bin\Debug-windows-x86_64\RedflagForge;%PATH%"
python tests\check_world_session.py bin-int\Tests\world_server.exe bin-int\Tests\WorldSessionTests.exe
exit /b %errorlevel%
