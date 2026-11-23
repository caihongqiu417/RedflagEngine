@echo off
setlocal
cd /d "%~dp0.."
if "%~1"=="--component-reflection" (
    python scripts\generate_component_fields.py --check
    if errorlevel 1 exit /b 1
)
set "RF_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%RF_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RF_VS=%%i"
if not defined RF_VS exit /b 1
call "%RF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "bin-int\Tests" mkdir "bin-int\Tests"
set "RF_TEST_SOURCE=tests\SceneRenderTests.cpp"
set "RF_TEST_NAME=SceneRenderTests"
if "%~1"=="--animation-montage" (
    set "RF_TEST_SOURCE=tests\AnimationMontageTests.cpp"
    set "RF_TEST_NAME=AnimationMontageTests"
)
if "%~1"=="--dev-context-panel" (
    set "RF_TEST_SOURCE=tests\DevContextPanelTests.cpp"
    set "RF_TEST_NAME=DevContextPanelTests"
    set "RF_TEST_EXTRA=bin-int\Debug-windows-x86_64\RedflagForge\DevContextPanel.obj bin-int\Debug-windows-x86_64\RedflagForge\NpcAuthoringFiles.obj"
)
if "%~1"=="--review-panel" (
    set "RF_TEST_SOURCE=tests\CreationReviewTests.cpp"
    set "RF_TEST_NAME=CreationReviewTests"
    set "RF_TEST_EXTRA=bin-int\Debug-windows-x86_64\RedflagForge\PlaytestReview.obj bin-int\Debug-windows-x86_64\RedflagForge\JobTimeline.obj bin-int\Debug-windows-x86_64\RedflagForge\AssetChangeReview.obj bin-int\Debug-windows-x86_64\RedflagForge\ReviewImage.obj bin-int\Debug-windows-x86_64\RedflagForge\NpcAuthoringFiles.obj"
    if not defined VULKAN_SDK for /f "delims=" %%i in ('dir /b /ad /o-n C:\VulkanSDK 2^>nul') do if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\%%i"
)
if "%~1"=="--station-companion" (
    set "RF_TEST_SOURCE=tests\StationCompanionTests.cpp"
    set "RF_TEST_NAME=StationCompanionTests"
    if not defined VULKAN_SDK for /f "delims=" %%i in ('dir /b /ad /o-n C:\VulkanSDK 2^>nul') do if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\%%i"
)
if "%~1"=="--decal" (
    set "RF_TEST_SOURCE=tests\DecalTests.cpp"
    set "RF_TEST_NAME=DecalTests"
    if not "%~3"=="" (
        for %%d in ("%~3") do set "VK_ADD_LAYER_PATH=%%~dpd"
        set "VK_INSTANCE_LAYERS=VK_LAYER_RENDERDOC_Capture"
    )
    set "RF_TEST_EXTRA=bin-int\Debug-windows-x86_64\RedflagForge\LightingPreview.obj"
    if not defined VULKAN_SDK for /f "delims=" %%i in ('dir /b /ad /o-n C:\VulkanSDK 2^>nul') do if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\%%i"
)
if "%~1"=="--height-material" (
    set "RF_TEST_SOURCE=tests\HeightMaterialTests.cpp"
    set "RF_TEST_NAME=HeightMaterialTests"
    set "RF_TEST_EXTRA=bin-int\Debug-windows-x86_64\RedflagForge\LightingPreview.obj"
    if not defined VULKAN_SDK for /f "delims=" %%i in ('dir /b /ad /o-n C:\VulkanSDK 2^>nul') do if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\%%i"
)
if "%~1"=="--foliage" (
    set "RF_TEST_SOURCE=tests\FoliageTests.cpp"
    set "RF_TEST_NAME=FoliageTests"
    set "RF_TEST_EXTRA=bin-int\Debug-windows-x86_64\RedflagForge\LightingPreview.obj"
    if not defined VULKAN_SDK for /f "delims=" %%i in ('dir /b /ad /o-n C:\VulkanSDK 2^>nul') do if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\%%i"
)
if "%~1"=="--npc-photo" (
    set "RF_TEST_SOURCE=tests\NpcCameraTests.cpp"
    set "RF_TEST_NAME=NpcCameraTests"
    set "RF_TEST_EXTRA=bin-int\Debug-windows-x86_64\RedflagForge\NpcCamera.obj bin-int\Debug-windows-x86_64\RedflagForge\LightingPreview.obj"
    if not defined VULKAN_SDK for /f "delims=" %%i in ('dir /b /ad /o-n C:\VulkanSDK 2^>nul') do if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\%%i"
)
if "%~1"=="--npc-memory" (
    set "RF_TEST_SOURCE=examples\npc-memory\MemoryHost.cpp"
    set "RF_TEST_NAME=MemoryHost"
)
if "%~1"=="--npc-voice-latency" (
    set "RF_TEST_SOURCE=tests\NpcVoiceLatency.cpp"
    set "RF_TEST_NAME=NpcVoiceLatency"
)
if "%~1"=="--npc-relationship" (
    set "RF_TEST_SOURCE=tests\NpcRelationshipTests.cpp"
    set "RF_TEST_NAME=NpcRelationshipTests"
)
if "%~1"=="--npc-environment" (
    set "RF_TEST_SOURCE=tests\EnvironmentZoneTests.cpp"
    set "RF_TEST_NAME=EnvironmentZoneTests"
)
if "%~1"=="--playtest-audio" (
    set "RF_TEST_SOURCE=tests\PlaytestAudioTests.cpp"
    set "RF_TEST_NAME=PlaytestAudioTests"
)
if "%~1"=="--coroutines" (
    set "RF_TEST_SOURCE=tests\CoroutineTests.cpp"
    set "RF_TEST_NAME=CoroutineTests"
)
cl /nologo /std:c++17 /EHsc /MDd /utf-8 /wd4828 /wd4005 /DRF_PLATFORM_WINDOWS /DGLFW_INCLUDE_NONE /DYAML_CPP_STATIC_DEFINE ^
 /I Redflag\vendor\entt\include /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\spdlog\include ^
 /I Redflag\vendor\fmt\include /I Redflag\vendor\glm /I Redflag\vendor\Glad\include /I Redflag\vendor\GLFW\include /I Redflag\vendor\yaml-cpp\include ^
 /I Redflag\vendor\imgui /I "%VULKAN_SDK%\Include" "%RF_TEST_SOURCE%" %RF_TEST_EXTRA% /Fe:bin-int\Tests\%RF_TEST_NAME%.exe /Fo:bin-int\Tests\%RF_TEST_NAME%.obj ^
 /link bin\Debug-windows-x86_64\Redflag\Redflag.lib bin\Debug-windows-x86_64\GLFW\GLFW.lib ^
 bin\Debug-windows-x86_64\Glad\Glad.lib Redflag\vendor\assimp\bin\Debug-windows-x86_64\assimp\assimp.lib Redflag\vendor\Box2D\bin\Debug-windows-x86_64\Box2D\Box2D.lib Redflag\vendor\imgui\bin\Debug-windows-x86_64\ImGui\ImGui.lib opengl32.lib user32.lib gdi32.lib shell32.lib advapi32.lib ^
 Redflag\vendor\yaml-cpp\bin\Debug-windows-x86_64\yaml-cpp\yaml-cpp.lib /NODEFAULTLIB:LIBCMTD /NODEFAULTLIB:LIBCPMTD
if errorlevel 1 exit /b 1
set "PATH=%CD%\bin\Debug-windows-x86_64\RedflagForge;%PATH%"
cd RedflagForge
..\bin-int\Tests\%RF_TEST_NAME%.exe %*
exit /b %errorlevel%
