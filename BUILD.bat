@echo off
REM Double-click to build PvZ Co-op and make PvZ-Coop-dist.zip.
REM It runs build.sh with MSYS2's bash. Does not start the game or touch saves.
cd /d "%~dp0"
"D:\Programs\C\usr\bin\bash.exe" -lc "cd \"$(cygpath -u '%~dp0')\" && ./build.sh %*"
echo.
pause
