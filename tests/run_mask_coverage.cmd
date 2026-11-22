@echo off
setlocal
cd /d "%~dp0.."
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RF_COVERAGE_VS=%%i"
if not defined RF_COVERAGE_VS exit /b 1
call "%RF_COVERAGE_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "bin-int\Tests" mkdir "bin-int\Tests"
cl /nologo /std:c++17 /EHsc /O2 /utf-8 tests\coverage\MaskCoverage.cpp tests\MaskCoverageTests.cpp ^
 /Fo:bin-int\Tests\ /Fe:bin-int\Tests\MaskCoverageTests.exe
if errorlevel 1 exit /b 1
bin-int\Tests\MaskCoverageTests.exe %*
exit /b %errorlevel%
