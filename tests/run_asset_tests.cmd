@echo off
setlocal
cd /d "%~dp0.."
set "RF_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%RF_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RF_VS=%%i"
if not defined RF_VS exit /b 1
call "%RF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "bin-int\Tests" mkdir "bin-int\Tests"
cl /nologo /std:c++17 /EHsc /utf-8 /wd4828 /DRF_PLATFORM_WINDOWS /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\spdlog\include /I Redflag\vendor\fmt\include tests\AssetManagerTests.cpp Redflag\src\Redflag\Assets\AssetLibrary.cpp /Fe:bin-int\Tests\AssetManagerTests.exe /Fo:bin-int\Tests\
if errorlevel 1 exit /b 1
bin-int\Tests\AssetManagerTests.exe
exit /b %errorlevel%
