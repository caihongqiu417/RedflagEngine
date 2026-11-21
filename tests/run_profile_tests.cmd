@echo off
setlocal
set "RF_TRACE=%~dp0..\output\profiling\redflag-cpu-trace.json"
if not "%~1"=="" set "RF_TRACE=%~f1"
if "%~1"=="--capture" set "RF_CAPTURE_DLL=%~f3"
if "%~1"=="--measure" set "RF_TIMING=%~f2"
cd /d "%~dp0.."
set "RF_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%RF_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RF_VS=%%i"
if not defined RF_VS exit /b 1
call "%RF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "bin-int\Tests" mkdir "bin-int\Tests"
cl /nologo /std:c++17 /EHsc /MDd /utf-8 /wd4828 /wd4005 /DRF_PLATFORM_WINDOWS /DGLFW_INCLUDE_NONE ^
 /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\spdlog\include ^
 /I Redflag\vendor\fmt\include /I Redflag\vendor\glm /I Redflag\vendor\Glad\include /I Redflag\vendor\GLFW\include ^
 tests\ProfileTraceTests.cpp /Fe:bin-int\Tests\ProfileTraceTests.exe /Fo:bin-int\Tests\ProfileTraceTests.obj ^
 /link bin\Debug-windows-x86_64\Redflag\Redflag.lib bin\Debug-windows-x86_64\GLFW\GLFW.lib ^
 bin\Debug-windows-x86_64\Glad\Glad.lib opengl32.lib user32.lib gdi32.lib shell32.lib ^
 /NODEFAULTLIB:LIBCMTD /NODEFAULTLIB:LIBCPMTD
if errorlevel 1 exit /b 1
set "PATH=%CD%\bin\Debug-windows-x86_64\RedflagForge;%PATH%"
cd RedflagForge
if "%~1"=="--capture" (
    ..\bin-int\Tests\ProfileTraceTests.exe --capture "%~2" "%RF_CAPTURE_DLL%"
    exit /b
)
if "%~1"=="--measure" (
    ..\bin-int\Tests\ProfileTraceTests.exe --measure "%RF_TIMING%"
    exit /b
)
..\bin-int\Tests\ProfileTraceTests.exe "%RF_TRACE%"
exit /b %errorlevel%
