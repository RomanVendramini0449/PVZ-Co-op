@echo off
REM Checks the netcode on ONE machine. Needs no network and no game files.
cd /d "%~dp0"

if not exist "PvZ-Coop.exe" (
    echo ERROR: PvZ-Coop.exe is not in this folder.
    pause
    exit /b 1
)

echo Running the co-op self-test (one machine, no network needed)...
echo.

PvZ-Coop.exe --coop-selftest
pause
