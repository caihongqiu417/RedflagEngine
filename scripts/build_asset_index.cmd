@echo off
setlocal
cd /d "%~dp0.."
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RF_VS=%%i"
call "%RF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "RF_OUTPUT=bin-int\Tools\Assets"
if not exist "%RF_OUTPUT%" mkdir "%RF_OUTPUT%"
cl /nologo /std:c++17 /EHsc /utf-8 /wd4828 /DRF_PLATFORM_WINDOWS /I Redflag\src /I Redflag\src\Redflag ^
 /I Redflag\vendor\spdlog\include /I Redflag\vendor\fmt\include scripts\AssetIndex.cpp Redflag\src\Redflag\Assets\AssetLibrary.cpp ^
 /Fo:%RF_OUTPUT%\ /Fe:%RF_OUTPUT%\AssetIndex.exe
exit /b %errorlevel%
