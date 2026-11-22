@echo off
setlocal
cd /d "%~dp0.."
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RF_VS=%%i"
call "%RF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist bin-int\Tests\Behavior mkdir bin-int\Tests\Behavior
set "PATH=%CD%\bin\Debug-windows-x86_64\RedflagForge;%PATH%"
cl /nologo /std:c++17 /EHsc /MDd /utf-8 /wd4828 /wd4005 /DRF_PLATFORM_WINDOWS /DYAML_CPP_STATIC_DEFINE ^
 /I Redflag\src /I Redflag\src\Redflag /I RedflagForge\src /I Redflag\vendor\glm /I Redflag\vendor\imgui ^
 /I Redflag\vendor\yaml-cpp\include /I Redflag\vendor\fmt\include /I Redflag\vendor\spdlog\include ^
 tests\BehaviorTreeTests.cpp Redflag\src\Redflag\AI\BehaviorTree.cpp RedflagForge\src\BehaviorTreePanel.cpp ^
 Redflag\src\Platform\Windows\WindowsFileSystem.cpp /Fo:bin-int\Tests\Behavior\ /Fe:bin-int\Tests\Behavior\BehaviorTreeTests.exe ^
 /link bin\Debug-windows-x86_64\Redflag\Redflag.lib Redflag\vendor\yaml-cpp\bin\Debug-windows-x86_64\yaml-cpp\yaml-cpp.lib ^
 Redflag\vendor\imgui\bin\Debug-windows-x86_64\ImGui\ImGui.lib user32.lib gdi32.lib imm32.lib ^
 /NODEFAULTLIB:LIBCMTD /NODEFAULTLIB:LIBCPMTD
if errorlevel 1 exit /b 1
bin-int\Tests\Behavior\BehaviorTreeTests.exe
if errorlevel 1 exit /b 1
if not exist bin-int\redflag-mcp\Scripts\python.exe (
 echo Install scripts/redflag_mcp/requirements.txt in bin-int/redflag-mcp before running the authoring panel test.
 exit /b 1
)
call scripts\redflag_mcp\build_npc_tool.cmd
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /MDd /utf-8 /wd4828 /wd4005 /DRF_PLATFORM_WINDOWS /DYAML_CPP_STATIC_DEFINE ^
 /I Redflag\src /I Redflag\src\Redflag /I RedflagForge\src /I Redflag\vendor\glm /I Redflag\vendor\imgui ^
 /I Redflag\vendor\yaml-cpp\include /I Redflag\vendor\fmt\include /I Redflag\vendor\spdlog\include ^
 /I Redflag\vendor\stb_image tests\NpcAuthoringPanelTests.cpp tests\NpcPanelCapture.cpp Redflag\vendor\imgui\backends\imgui_impl_dx11.cpp ^
 RedflagForge\src\NpcAuthoringPanel.cpp RedflagForge\src\NpcAuthoringFiles.cpp RedflagForge\src\AnimationPoseView.cpp ^
 Redflag\src\Redflag\AI\NpcDefinition.cpp Redflag\src\Redflag\AI\DecisionClient.cpp Redflag\src\Redflag\AI\LocalService.cpp ^
 Redflag\src\Redflag\Animation\AnimationController.cpp ^
 bin-int\Tests\Behavior\BehaviorTree.obj bin-int\Tests\Behavior\WindowsFileSystem.obj ^
 /Fo:bin-int\Tests\Behavior\ /Fe:bin-int\Tests\Behavior\NpcAuthoringPanelTests.exe ^
 /link bin\Debug-windows-x86_64\Redflag\Redflag.lib Redflag\vendor\yaml-cpp\bin\Debug-windows-x86_64\yaml-cpp\yaml-cpp.lib ^
 Redflag\vendor\imgui\bin\Debug-windows-x86_64\ImGui\ImGui.lib user32.lib gdi32.lib imm32.lib ^
 /NODEFAULTLIB:LIBCMTD /NODEFAULTLIB:LIBCPMTD d3d11.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
bin-int\Tests\Behavior\NpcAuthoringPanelTests.exe %*
exit /b %errorlevel%
