@echo off
setlocal
cd /d "%~dp0"
powershell -NoLogo -NoProfile -Command "$manifest = Get-Content -LiteralPath 'asset-manifest.json' -Raw -Encoding UTF8 | ConvertFrom-Json; & '.\RedflagForge.exe' --vulkan --play $manifest.scene; exit $LASTEXITCODE"
exit /b %errorlevel%
