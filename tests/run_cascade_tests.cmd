@echo off
setlocal
set "RF_CASCADE_TEST=1"
call "%~dp0run_taa_camera_tests.cmd" %*
exit /b %errorlevel%
