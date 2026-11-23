@echo off
setlocal
cd /d "%~dp0..\.."
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RF_VS=%%i"
call "%RF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "RF_OUTPUT=bin-int\Tools\Npc"
if not exist "%RF_OUTPUT%" mkdir "%RF_OUTPUT%"
cl /nologo /std:c++17 /EHsc /MDd /utf-8 /wd4828 /wd4005 /DRF_PLATFORM_WINDOWS /DYAML_CPP_STATIC_DEFINE ^
 /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\glm /I Redflag\vendor\yaml-cpp\include ^
 /I Redflag\vendor\fmt\include /I Redflag\vendor\spdlog\include ^
 scripts\redflag_mcp\NpcTool.cpp Redflag\src\Redflag\AI\NpcDefinition.cpp Redflag\src\Redflag\AI\BehaviorTree.cpp Redflag\src\Redflag\AI\BehaviorTasks.cpp ^
 Redflag\src\Redflag\Animation\AnimationController.cpp Redflag\src\Redflag\Animation\Montage.cpp Redflag\src\Redflag\Core\UUID.cpp ^
 Redflag\src\Platform\Windows\WindowsFileSystem.cpp /Fo:%RF_OUTPUT%\ /Fe:%RF_OUTPUT%\NpcTool.exe ^
 /link Redflag\vendor\yaml-cpp\bin\Debug-windows-x86_64\yaml-cpp\yaml-cpp.lib /NODEFAULTLIB:LIBCMTD /NODEFAULTLIB:LIBCPMTD
exit /b %errorlevel%
