@echo off
setlocal
cd /d "%~dp0RedflagForge"
set "forge=..\bin\Debug-windows-x86_64\RedflagForge\RedflagForge.exe"
if not exist "%forge%" (
    echo Build RedflagForge in Debug x64 with Redflag.sln first.
    pause
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "..\scripts\Play-Station.ps1" -Editor %*
exit /b %errorlevel%
