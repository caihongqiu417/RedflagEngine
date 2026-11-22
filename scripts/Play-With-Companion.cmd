@echo off
setlocal
cd /d "%~dp0"
powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File scripts\Start-NpcServices.ps1 -RequireAll
if errorlevel 1 (
    echo Check the model and speech settings in the guide. Press any key to close.
    pause >nul
    exit /b 1
)
call Play.cmd
