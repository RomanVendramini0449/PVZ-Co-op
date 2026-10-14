@echo off
REM Opens the co-op launcher window. Double-click this.
REM
REM -ExecutionPolicy Bypass applies to this one run only; it does not change
REM any Windows setting. It is needed because PowerShell blocks script files
REM by default. The script it runs is the plain text file sitting next to
REM this one, which you can open and read.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0PVZ-Coop-Launcher.ps1"
if errorlevel 1 (
    echo.
    echo The launcher could not start. You can still run the game from a
    echo command line - see docs\SETUP.md.
    pause
)
