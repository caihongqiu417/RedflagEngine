@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\GenerateProjects.ps1"
exit /b %errorlevel%
