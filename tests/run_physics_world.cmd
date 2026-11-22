@echo off
setlocal
cd /d "%~dp0.."
set "RF_SDK=bin-int\dependencies\PhysX"
if not exist "%RF_SDK%\lib\debug\PhysX_static_64.lib" (
    echo Build the SDK with scripts\BuildPhysX.ps1 first.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RF_VS=%%i"
call "%RF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist bin-int\Tests mkdir bin-int\Tests
cl /nologo /std:c++17 /EHsc /MDd /utf-8 /DRF_PLATFORM_WINDOWS /DPX_DEBUG=1 /DPX_CHECKED=1 ^
 /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\glm /I Redflag\vendor\spdlog\include /I "%RF_SDK%\include" ^
 Redflag\src\Redflag\Physics\PhysicsWorld.cpp tests\PhysicsWorldTests.cpp /Fo:bin-int\Tests\ /Fe:bin-int\Tests\PhysicsWorldTests.exe ^
 /link /LIBPATH:"%RF_SDK%\lib\debug" PhysX_static_64.lib PhysXFoundation_static_64.lib PhysXCommon_static_64.lib PhysXExtensions_static_64.lib PhysXPvdSDK_static_64.lib PhysXCooking_static_64.lib
if errorlevel 1 exit /b 1
bin-int\Tests\PhysicsWorldTests.exe
exit /b %errorlevel%
