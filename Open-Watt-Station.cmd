@echo off
setlocal
cd /d "%~dp0"
set "forge=..\bin\Debug-windows-x86_64\RedflagForge\RedflagForge.exe"
if not exist "RedflagForge\%forge%" (
    echo Build RedflagForge Debug x64 before starting the game.
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\Start-NpcServices.ps1"
cd RedflagForge
start "" "%forge%" --vulkan --play "assets/scenes/Watt-Station.redflag" %*
