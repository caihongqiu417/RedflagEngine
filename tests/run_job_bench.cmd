@echo off
setlocal
cd /d "%~dp0.."
set "RF_LOCATOR=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%RF_LOCATOR%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RF_VS=%%i"
call "%RF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "bin-int\Tests\job-bench" mkdir "bin-int\Tests\job-bench"
python -X utf8 tests\job_bench.py prepare
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /O2 /DNDEBUG /EHsc /MD /utf-8 /DRF_PLATFORM_WINDOWS /DNOMINMAX /wd4005 /wd4828 ^
 /I Redflag\src /I Redflag\src\Redflag /I Redflag\vendor\entt\include /I Redflag\vendor\glm /I Redflag\vendor\spdlog\include /I Redflag\vendor\fmt\include /I Redflag\vendor\stb_image ^
 tests\TerrainJobBench.cpp Redflag\src\Redflag\Core\JobSystem.cpp Redflag\src\Redflag\Terrain\Terrain.cpp ^
 /Fo:bin-int\Tests\job-bench\ /Fe:bin-int\Tests\job-bench\TerrainJobBench.exe
if errorlevel 1 exit /b 1
if /I "%~1"=="--verify" (
    bin-int\Tests\job-bench\TerrainJobBench.exe --verify
    exit /b
)
bin-int\Tests\job-bench\TerrainJobBench.exe RedflagForge\assets\terrain\quarry\quarry-height.png output\profiling\jobs bin-int\Tests\job-bench\transforms.txt
if errorlevel 1 exit /b 1
python -X utf8 tests\job_bench.py summary
