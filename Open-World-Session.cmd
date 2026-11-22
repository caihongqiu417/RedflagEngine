@echo off
setlocal
cd /d "%~dp0"
set "RF_WORLD_ADDRESS=%~2"
set "RF_WORLD_PORT=%~3"
if not defined RF_WORLD_ADDRESS set "RF_WORLD_ADDRESS=127.0.0.1"
if not defined RF_WORLD_PORT set "RF_WORLD_PORT=42017"
if /i "%~1"=="server" goto server
if /i "%~1"=="host" goto client
if /i "%~1"=="watch" goto client
echo Open-World-Session server [local IPv4] [port]
echo Open-World-Session host^|watch [server IPv4] [port]
exit /b 2

:server
if not exist bin-int\Tests\world_server.exe call tests\run_world_session.cmd --build || exit /b 1
bin-int\Tests\world_server.exe "%RF_WORLD_PORT%" 500 10000 "%RF_WORLD_ADDRESS%"
exit /b %errorlevel%

:client
if not exist bin-int\Tests\WorldWorkshop.exe call tests\run_world_session.cmd --build || exit /b 1
set "PATH=%CD%\bin\Debug-windows-x86_64\RedflagForge;%PATH%"
bin-int\Tests\WorldWorkshop.exe "%~1" "%RF_WORLD_ADDRESS%" "%RF_WORLD_PORT%"
exit /b %errorlevel%
