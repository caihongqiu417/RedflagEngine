@echo off
setlocal
cd /d "%~dp0"
if not exist "bin-int\Tests\Character\CharacterAnimationTests.exe" (
    call tests\run_character_animation.cmd --demo
    exit /b
)
cd RedflagForge
"..\bin-int\Tests\Character\CharacterAnimationTests.exe" --demo
