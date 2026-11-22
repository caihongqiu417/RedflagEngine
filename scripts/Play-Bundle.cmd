@echo off
setlocal
cd /d "%~dp0"
powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "scripts\Play-Bundle.ps1" %*
exit /b %errorlevel%
