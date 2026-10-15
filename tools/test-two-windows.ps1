param([string]$Difficulty = 'normal')

# Starts two copies of the release build on this PC, one hosting and one
# joining it, side by side -- for looking around the game, not for playing
# with your friend.
#
# Your real save is never touched: each window keeps its own test save in
# test-saves\host and test-saves\join next to this project (made the first
# time; delete the folder to start fresh). It uses its own port, 1347, so
# it doesn't clash with a real session on 1337, and it doesn't close any
# game you already have open.

$Root = Split-Path -Parent $PSScriptRoot
$Exe = Join-Path $Root 'build-dist\package\PvZ-Coop\PvZ-Coop.exe'
$GameDir = 'G:\Steam\steamapps\common\Plants Vs Zombies'
$Port = 1347

if (-not (Test-Path $Exe)) {
    Write-Host "No build yet: $Exe"
    Write-Host 'Run BUILD.bat first.'
    exit 1
}
if (-not (Test-Path (Join-Path $GameDir 'main.pak'))) {
    Write-Host "Plants vs. Zombies is not at $GameDir -- edit `$GameDir in tools\test-two-windows.ps1."
    exit 1
}

# Forward slashes, ending in one: the game appends file names straight on.
$HostSave = (Join-Path $Root 'test-saves\host') -replace '\\', '/'
$JoinSave = (Join-Path $Root 'test-saves\join') -replace '\\', '/'
New-Item -ItemType Directory -Force -Path $HostSave, $JoinSave | Out-Null

$HostArgs = "-resdir `"$GameDir`" -savedir `"$HostSave/`" --host --port $Port --difficulty $Difficulty"
$JoinArgs = "-resdir `"$GameDir`" -savedir `"$JoinSave/`" --join 127.0.0.1:$Port"

Write-Host "Host:  $HostArgs"
$hostProcess = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -ArgumentList $HostArgs -PassThru
Start-Sleep -Milliseconds 1500
Write-Host "Join:  $JoinArgs"
$joinProcess = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -ArgumentList $JoinArgs -PassThru

# Side by side once both windows are up.
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class CoopTestWindows {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
"@
$deadline = (Get-Date).AddSeconds(60)
while ((Get-Date) -lt $deadline) {
    $hostProcess.Refresh(); $joinProcess.Refresh()
    if ($hostProcess.MainWindowHandle -ne 0 -and $joinProcess.MainWindowHandle -ne 0) { break }
    Start-Sleep -Milliseconds 300
}
# SWP_NOSIZE | SWP_NOZORDER
if ($hostProcess.MainWindowHandle -ne 0) { [CoopTestWindows]::SetWindowPos($hostProcess.MainWindowHandle, [IntPtr]::Zero, 0, 0, 0, 0, 5) | Out-Null }
if ($joinProcess.MainWindowHandle -ne 0) { [CoopTestWindows]::SetWindowPos($joinProcess.MainWindowHandle, [IntPtr]::Zero, 830, 0, 0, 0, 5) | Out-Null }
Write-Host 'Both windows started. The first time, each asks for a player name.'
