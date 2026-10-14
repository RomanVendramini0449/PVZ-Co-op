@echo off
REM Double-click this on the HOST machine. No command line needed.
cd /d "%~dp0"

if not exist "PvZ-Coop.exe" (
    echo ERROR: PvZ-Coop.exe is not in this folder.
    echo Put this script next to the game executable and try again.
    pause
    exit /b 1
)

echo ==========================================================
echo  CO-OP NETWORK TEST - HOST
echo ==========================================================
echo.
echo This does NOT start the game. It tests the connection
echo between the two machines and then exits.
echo.
echo Start this FIRST, then run 2-JOIN-nettest.bat on the
echo other machine. This will wait up to 2 minutes for them.
echo.
echo If Windows Firewall asks, click "Allow access".
echo.

PvZ-Coop.exe --coop-nettest --host --port 1337

echo.
echo ==========================================================
echo  Saved to coop-nettest-host.txt in this folder.
echo  Send that file over along with the other machine's.
echo ==========================================================
pause
