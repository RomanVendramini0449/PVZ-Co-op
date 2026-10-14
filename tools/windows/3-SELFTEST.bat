@echo off
REM Checks the netcode on ONE machine. Needs no network and no game files.
cd /d "%~dp0"

if not exist "pvz-portable.exe" (
    echo ERROR: pvz-portable.exe is not in this folder.
    pause
    exit /b 1
)

echo Running the co-op self-test (one machine, no network needed)...
echo.

pvz-portable.exe --coop-selftest
pause
