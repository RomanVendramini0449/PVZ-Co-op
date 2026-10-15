@echo off
REM Opens two game windows on this PC, one hosting and one joining it, for
REM looking around. Uses test saves in test-saves\, never your real save.
REM Optional: a difficulty, e.g.  TEST-TWO-WINDOWS.bat nightmare
cd /d "%~dp0"
if "%~1"=="" (set DIFF=normal) else (set DIFF=%~1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\test-two-windows.ps1" -Difficulty %DIFF%
if errorlevel 1 pause
