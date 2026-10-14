@echo off
REM Double-click this on the SECOND machine, after the host has started.
cd /d "%~dp0"

if not exist "pvz-portable.exe" (
    echo ERROR: pvz-portable.exe is not in this folder.
    echo Put this script next to the game executable and try again.
    pause
    exit /b 1
)

echo ==========================================================
echo  CO-OP NETWORK TEST - JOIN
echo ==========================================================
echo.
echo This does NOT start the game. It tests the connection
echo between the two machines and then exits.
echo.
echo Run 1-HOST-nettest.bat on the other machine FIRST. It
echo prints the address to type in below.
echo.

set "HOSTIP="
set /p HOSTIP="Host IP address (for example 192.168.1.42): "

if "%HOSTIP%"=="" (
    echo.
    echo No address entered, nothing to connect to.
    pause
    exit /b 1
)

echo.
echo Connecting to %HOSTIP%:1337 ...
echo.

pvz-portable.exe --coop-nettest --join %HOSTIP%:1337

echo.
echo ==========================================================
echo  Saved to coop-nettest-join.txt in this folder.
echo  Send that file over along with the host's.
echo ==========================================================
pause
