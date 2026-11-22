@echo off
setlocal
cd /d "%~dp0"
set "REDFLAG_BUNDLE_COMPANION=1"
Play.cmd %*
