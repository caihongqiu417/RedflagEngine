@echo off
setlocal
cd /d "%~dp0.."
set "RF_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%RF_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RF_VS=%%i"
if not defined RF_VS exit /b 1
call "%RF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "bin-int\Tests\Decision" mkdir "bin-int\Tests\Decision"
cl /nologo /std:c++17 /MDd /EHsc /utf-8 /wd4828 /wd4005 /DRF_PLATFORM_WINDOWS /DYAML_CPP_STATIC_DEFINE ^
 /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\spdlog\include /I Redflag\vendor\fmt\include /I Redflag\vendor\yaml-cpp\include ^
 tests\DecisionClientTests.cpp Redflag\src\Redflag\AI\DecisionClient.cpp Redflag\src\Redflag\AI\LocalService.cpp /Fo:bin-int\Tests\Decision\ /Fe:bin-int\Tests\Decision\DecisionClientTests.exe ^
 /link Redflag\vendor\yaml-cpp\bin\Debug-windows-x86_64\yaml-cpp\yaml-cpp.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /MDd /EHsc /utf-8 /wd4828 /wd4005 /DRF_PLATFORM_WINDOWS /DYAML_CPP_STATIC_DEFINE ^
 /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\spdlog\include /I Redflag\vendor\fmt\include /I Redflag\vendor\yaml-cpp\include ^
 tests\SpeechInputTests.cpp Redflag\src\Redflag\AI\SpeechInput.cpp bin-int\Tests\Decision\LocalService.obj /Fo:bin-int\Tests\Decision\ /Fe:bin-int\Tests\Decision\SpeechInputTests.exe ^
 /link Redflag\vendor\yaml-cpp\bin\Debug-windows-x86_64\yaml-cpp\yaml-cpp.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /MDd /EHsc /utf-8 /wd4828 /wd4005 /DRF_PLATFORM_WINDOWS /DYAML_CPP_STATIC_DEFINE ^
 /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\spdlog\include /I Redflag\vendor\fmt\include /I Redflag\vendor\yaml-cpp\include ^
 tests\SpeechPlaybackTests.cpp Redflag\src\Redflag\AI\SpeechPlayback.cpp bin-int\Tests\Decision\LocalService.obj /Fo:bin-int\Tests\Decision\ /Fe:bin-int\Tests\Decision\SpeechPlaybackTests.exe ^
 /link Redflag\vendor\yaml-cpp\bin\Debug-windows-x86_64\yaml-cpp\yaml-cpp.lib
exit /b %errorlevel%
