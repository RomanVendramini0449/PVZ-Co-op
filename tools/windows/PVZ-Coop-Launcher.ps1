param([switch]$TestOnly)

# PVZ Co-op Launcher
#
# A small window for starting a co-op session without typing command lines.
# Launched by PVZ-Coop-Launcher.bat, which sits next to it.
#
# It only ever builds a command line and starts PvZ-Coop.exe with it.
# Nothing here touches your Plants vs. Zombies install.

if (-not $TestOnly) {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [System.Windows.Forms.Application]::EnableVisualStyles()
}

# $PSScriptRoot is the reliable one; the fallback covers odd invocations.
$ScriptDir = $PSScriptRoot
if (-not $ScriptDir) { $ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition }
if (-not $ScriptDir) { $ScriptDir = (Get-Location).Path }
$ExePath   = Join-Path $ScriptDir 'PvZ-Coop.exe'
$ConfigPath = Join-Path $ScriptDir 'coop-launcher-settings.txt'

# ---------------------------------------------------------------- settings --
# The host's difficulty choices, in the order the game's own menu cycles
# through them. Key is what --difficulty takes; the host can still change it
# from the main menu between levels.
$Difficulties = @(
    @{ Key = 'normal';    Label = 'Normal' },
    @{ Key = 'hard';      Label = 'Hard  (sun worth 15, zombies 1.2x tougher)' },
    @{ Key = 'wild';      Label = 'Wild  (normal sun, zombies 1x to 2x tougher)' },
    @{ Key = 'horde';     Label = 'Horde  (1.5x the zombies, 1x to 2x tougher)' },
    @{ Key = 'nightmare'; Label = 'Nightmare  (all of it: Hard sun, Horde zombies)' },
    @{ Key = 'inferno';   Label = 'Inferno  (2x zombies, 1.5x-2.5x tougher, slow recharge)' },
    @{ Key = 'apocalypse'; Label = 'Apocalypse  (sun worth 10, 2.5x zombies, 2x-3x tougher)' }
)

# Remembered between runs so nobody re-types a path or an address every time.
$Settings = @{ GameDir = ''; Mode = 'host'; Address = ''; Port = '1337'; Difficulty = 'normal' }

if (Test-Path $ConfigPath) {
    foreach ($line in Get-Content $ConfigPath) {
        $parts = $line -split '=', 2
        if ($parts.Count -eq 2 -and $Settings.ContainsKey($parts[0])) {
            $Settings[$parts[0]] = $parts[1]
        }
    }
}

function Save-Settings {
    $lines = foreach ($key in $Settings.Keys) { "$key=$($Settings[$key])" }
    try { Set-Content -Path $ConfigPath -Value $lines -Encoding UTF8 } catch { }
}

# Find the game without being told where it is.
#
# Asks Steam rather than guessing: the registry gives Steam's own location,
# and libraryfolders.vdf lists every library folder the user has added, which
# is the part a list of guessed paths always gets wrong. Guesses are still
# tried afterwards, for non-Steam copies and for anything the registry misses.

function Get-SteamRoots {
    $roots = @()
    foreach ($key in @('HKCU:\Software\Valve\Steam',
                       'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam',
                       'HKLM:\SOFTWARE\Valve\Steam')) {
        try {
            $item = Get-ItemProperty -Path $key -ErrorAction Stop
            foreach ($prop in @('SteamPath', 'InstallPath')) {
                if ($item.$prop) { $roots += [string]$item.$prop }
            }
        } catch { }
    }
    foreach ($drive in [System.IO.DriveInfo]::GetDrives()) {
        if (-not $drive.IsReady) { continue }
        $r = $drive.RootDirectory.FullName
        $roots += (Join-Path $r 'Program Files (x86)\Steam')
        $roots += (Join-Path $r 'Program Files\Steam')
        $roots += (Join-Path $r 'Steam')
        $roots += (Join-Path $r 'SteamLibrary')
    }
    return $roots
}

# Every library Steam knows about, including ones on other drives.
function Get-SteamLibraries {
    $libraries = @()
    foreach ($root in Get-SteamRoots) {
        if (-not $root) { continue }
        if (Test-Path (Join-Path $root 'steamapps')) { $libraries += $root }

        $vdf = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            try {
                foreach ($line in Get-Content $vdf -ErrorAction Stop) {
                    # Entries look like:   "path"   "G:\\SteamLibrary"
                    $m = [regex]::Match($line, '"path"\s+"(.+?)"')
                    if ($m.Success) {
                        # VDF escapes its backslashes.
                        $libraries += $m.Groups[1].Value -replace '\\\\', '\'
                    }
                }
            } catch { }
        }
    }
    return $libraries | Select-Object -Unique
}

function Get-GameCandidates {
    $found = @()

    foreach ($library in Get-SteamLibraries) {
        $common = Join-Path $library 'steamapps\common'
        if (-not (Test-Path $common)) { continue }
        try {
            foreach ($dir in Get-ChildItem -Path $common -Directory -ErrorAction Stop) {
                if ($dir.Name -notmatch 'Plants') { continue }
                if (Test-Path (Join-Path $dir.FullName 'main.pak')) { $found += $dir.FullName }
            }
        } catch { }
    }

    # Non-Steam copies: retail, Origin and the EA app all land near here.
    foreach ($drive in [System.IO.DriveInfo]::GetDrives()) {
        if (-not $drive.IsReady) { continue }
        $r = $drive.RootDirectory.FullName
        foreach ($guess in @(
            'Program Files (x86)\PopCap Games\Plants vs. Zombies',
            'Program Files\PopCap Games\Plants vs. Zombies',
            'Program Files (x86)\Origin Games\Plants vs Zombies',
            'Program Files (x86)\EA Games\Plants vs. Zombies',
            'Program Files\EA Games\Plants vs. Zombies',
            'Games\Plants vs. Zombies',
            'Plants vs. Zombies')) {
            $path = Join-Path $r $guess
            if (Test-Path (Join-Path $path 'main.pak')) { $found += $path }
        }
    }

    # A GOTY install is the one this mod targets, so float it to the top.
    $ranked = @($found | Select-Object -Unique | Sort-Object -Property @{
        Expression = { if ($_ -match 'GOTY') { 0 } else { 1 } }
    }, @{ Expression = { $_ } })

    return $ranked
}

# The folders the launcher itself is in: the one it sits in, the ones above
# it, and wherever it was started from. Unzipping the mod straight into the
# game folder (or a folder inside it) is the simplest setup there is, and it
# should simply work.
function Get-NearbyGameDir {
    $starts = @($ScriptDir)
    try { $starts += (Get-Location).Path } catch { }
    foreach ($start in $starts) {
        $dir = $start
        for ($depth = 0; $dir -and $depth -lt 4; $depth++) {
            if (Test-Path (Join-Path $dir 'main.pak')) { return $dir }
            $dir = Split-Path -Parent $dir
        }
    }
    return ''
}

function Find-GameDir {
    # Whatever worked last time wins, so a deliberate choice is never
    # overridden by detection.
    if ($Settings.GameDir -and (Test-Path (Join-Path $Settings.GameDir 'main.pak'))) {
        return $Settings.GameDir
    }
    $nearby = Get-NearbyGameDir
    if ($nearby) { return $nearby }
    $candidates = Get-GameCandidates
    if ($candidates.Count -gt 0) { return $candidates[0] }
    return ''
}

# The normal Windows "Open" window, pointed at the game's own files: pick
# PlantsVsZombies.exe or main.pak and the folder they are in is the answer.
# (The folder-only picker .NET offers is the old tree view, which is hard to
# find anything in.)
function Select-GameDir([string]$StartDir) {
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title = 'Find Plants vs. Zombies: pick PlantsVsZombies.exe or main.pak in its folder'
    $dlg.Filter = 'Plants vs. Zombies (PlantsVsZombies.exe, main.pak)|PlantsVsZombies.exe;main.pak;*.exe|All files (*.*)|*.*'
    $dlg.CheckFileExists = $true
    $dlg.Multiselect = $false
    if ($StartDir -and (Test-Path $StartDir)) { $dlg.InitialDirectory = $StartDir }
    if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { return '' }
    return (Split-Path -Parent $dlg.FileName)
}

# Start-Process joins an argument array with spaces and quotes nothing, so a
# path like "C:\Program Files (x86)\..." would arrive at the game as several
# separate arguments and it would die on the first stray word. Everything is
# quoted here instead, and the command line is passed as one string.
function ConvertTo-QuotedArg([string]$Value) {
    if ($Value -eq '') { return '""' }
    if ($Value -notmatch '[ \t"]') { return $Value }
    # Trailing backslashes would escape the closing quote under the C
    # runtime's parsing rules, so each run that reaches the end is doubled.
    $escaped = $Value -replace '(\\*)$', '$1$1'
    $escaped = $escaped -replace '"', '\"'
    return '"' + $escaped + '"'
}

function Get-LocalAddresses {
    try {
        return @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction Stop |
            Where-Object { $_.IPAddress -ne '127.0.0.1' -and $_.IPAddress -notlike '169.254.*' } |
            Select-Object -ExpandProperty IPAddress)
    } catch {
        # Older Windows without the NetTCPIP module.
        try {
            return @([System.Net.Dns]::GetHostAddresses([System.Net.Dns]::GetHostName()) |
                Where-Object { $_.AddressFamily -eq 'InterNetwork' } |
                ForEach-Object { $_.IPAddressToString } |
                Where-Object { $_ -ne '127.0.0.1' })
        } catch { return @() }
    }
}

# Builds the exact command line the game is started with. Kept out of the
# button handler so it can be tested without a window: run this script with
# -TestOnly to check both the host and join forms.
function Build-GameCommandLine {
    param(
        [string]$GameDir,
        [bool]$IsHost,
        [string]$Address,
        [int]$Port,
        [string]$Difficulty = 'normal'
    )

    $argList = @('-resdir', $GameDir)

    if ($IsHost) {
        $argList += '--host'
        $argList += '--port'
        $argList += "$Port"
        if ($Difficulty -and $Difficulty -ne 'normal') { $argList += '--difficulty'; $argList += $Difficulty }
    } else {
        $target = $Address.Trim()
        # Accept a bare address or one that already carries a port.
        if ($target -notmatch ':') { $target = "${target}:${Port}" }
        $argList += '--join'
        $argList += $target
    }

    return ($argList | ForEach-Object { ConvertTo-QuotedArg $_ }) -join ' '
}

# ------------------------------------------------------------------- self test --
if ($TestOnly) {
    $failures = 0
    function Assert-Line($Label, $Actual, $Expected) {
        if ($Actual -ceq $Expected) {
            Write-Host "  OK   $Label"
            Write-Host "         $Actual"
        } else {
            Write-Host "  FAIL $Label"
            Write-Host "         got      $Actual"
            Write-Host "         expected $Expected"
            $script:failures++
        }
    }

    Write-Host 'launcher command line test'

    Assert-Line 'host, path with spaces' `
        (Build-GameCommandLine -GameDir 'G:\Steam\steamapps\common\Plants Vs Zombies' -IsHost $true -Address '' -Port 1337) `
        '-resdir "G:\Steam\steamapps\common\Plants Vs Zombies" --host --port 1337'

    Assert-Line 'host, Program Files, hard' `
        (Build-GameCommandLine -GameDir 'C:\Program Files (x86)\Steam\steamapps\common\Plants vs. Zombies GOTY' -IsHost $true -Address '' -Port 1337 -Difficulty 'hard') `
        '-resdir "C:\Program Files (x86)\Steam\steamapps\common\Plants vs. Zombies GOTY" --host --port 1337 --difficulty hard'

    Assert-Line 'host, nightmare' `
        (Build-GameCommandLine -GameDir 'C:\pvz' -IsHost $true -Address '' -Port 1337 -Difficulty 'nightmare') `
        '-resdir C:\pvz --host --port 1337 --difficulty nightmare'

    Assert-Line 'join ignores the difficulty' `
        (Build-GameCommandLine -GameDir 'C:\pvz' -IsHost $false -Address '192.168.2.40' -Port 1337 -Difficulty 'horde') `
        '-resdir C:\pvz --join 192.168.2.40:1337'

    Assert-Line 'join, bare address gains the port' `
        (Build-GameCommandLine -GameDir 'G:\Steam\steamapps\common\Plants Vs Zombies' -IsHost $false -Address '192.168.2.40' -Port 1337) `
        '-resdir "G:\Steam\steamapps\common\Plants Vs Zombies" --join 192.168.2.40:1337'

    Assert-Line 'join, address already has a port' `
        (Build-GameCommandLine -GameDir 'C:\pvz' -IsHost $false -Address '10.0.0.5:1400' -Port 1337) `
        '-resdir C:\pvz --join 10.0.0.5:1400'

    Assert-Line 'join, surrounding spaces trimmed' `
        (Build-GameCommandLine -GameDir 'C:\pvz' -IsHost $false -Address '  192.168.2.40  ' -Port 1337) `
        '-resdir C:\pvz --join 192.168.2.40:1337'

    Assert-Line 'trailing separator is escaped' `
        (Build-GameCommandLine -GameDir 'D:\My Games\PvZ\' -IsHost $true -Address '' -Port 1337) `
        '-resdir "D:\My Games\PvZ\\" --host --port 1337'

    # The launcher finds a game it was unzipped into, or into a folder of.
    $fakeGame = Join-Path ([System.IO.Path]::GetTempPath()) ("pvz-launcher-test-" + [guid]::NewGuid().ToString('N'))
    $fakeMod = Join-Path $fakeGame 'PvZ-Coop'
    New-Item -ItemType Directory -Path $fakeMod -Force | Out-Null
    Set-Content -Path (Join-Path $fakeGame 'main.pak') -Value 'test'
    $savedScriptDir = $ScriptDir
    $script:ScriptDir = $fakeMod
    Assert-Line 'finds the game in a parent folder' (Get-NearbyGameDir) $fakeGame
    $script:ScriptDir = $fakeGame
    Assert-Line 'finds the game in its own folder' (Get-NearbyGameDir) $fakeGame
    $script:ScriptDir = $savedScriptDir
    Remove-Item -Recurse -Force $fakeGame

    if ($failures -gt 0) {
        Write-Host ""
        Write-Host "$failures case(s) failed"
        exit 1
    }
    Write-Host ""
    Write-Host 'all launcher command lines correct'
    exit 0
}

# ------------------------------------------------------------------- window --
$form = New-Object System.Windows.Forms.Form
$form.Text = 'Plants vs. Zombies Co-op'
$form.Size = New-Object System.Drawing.Size(470, 420)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false

function New-Label($text, $x, $y, $w, $h) {
    $l = New-Object System.Windows.Forms.Label
    $l.Text = $text; $l.Location = New-Object System.Drawing.Point($x, $y)
    $l.Size = New-Object System.Drawing.Size($w, $h)
    return $l
}

# Game folder
$form.Controls.Add((New-Label 'Plants vs. Zombies folder' 15 12 250 18))

$txtDir = New-Object System.Windows.Forms.TextBox
$txtDir.Location = New-Object System.Drawing.Point(15, 32)
$txtDir.Size = New-Object System.Drawing.Size(330, 22)
$txtDir.Text = Find-GameDir
$form.Controls.Add($txtDir)

$btnBrowse = New-Object System.Windows.Forms.Button
$btnBrowse.Text = 'Browse'
$btnBrowse.Location = New-Object System.Drawing.Point(352, 31)
$btnBrowse.Size = New-Object System.Drawing.Size(85, 24)
$btnBrowse.Add_Click({
    $picked = Select-GameDir $txtDir.Text
    if (-not $picked) { return }
    if (-not (Test-Path (Join-Path $picked 'main.pak'))) {
        [System.Windows.Forms.MessageBox]::Show(
            "There is no main.pak next to that file.`r`n`r`nPick PlantsVsZombies.exe in your Plants vs. Zombies folder.",
            'Co-op Launcher', 'OK', 'Warning') | Out-Null
    }
    $txtDir.Text = $picked
})
$form.Controls.Add($btnBrowse)

$lblDirState = New-Label '' 15 58 420 18
$lblDirState.ForeColor = [System.Drawing.Color]::DimGray
$form.Controls.Add($lblDirState)

# Host or join
$grp = New-Object System.Windows.Forms.GroupBox
$grp.Text = 'How do you want to play?'
$grp.Location = New-Object System.Drawing.Point(15, 84)
$grp.Size = New-Object System.Drawing.Size(422, 186)

$rbHost = New-Object System.Windows.Forms.RadioButton
$rbHost.Text = 'Host a game  (your friend connects to you)'
$rbHost.Location = New-Object System.Drawing.Point(14, 26)
$rbHost.Size = New-Object System.Drawing.Size(390, 22)
$grp.Controls.Add($rbHost)

$rbJoin = New-Object System.Windows.Forms.RadioButton
$rbJoin.Text = 'Join a game  (connect to your friend)'
$rbJoin.Location = New-Object System.Drawing.Point(14, 50)
$rbJoin.Size = New-Object System.Drawing.Size(390, 22)
$grp.Controls.Add($rbJoin)

# Host-only: difficulty, and this machine's addresses to read out.
$lblDiff = New-Label 'Difficulty' 34 82 70 18
$grp.Controls.Add($lblDiff)

$cmbDiff = New-Object System.Windows.Forms.ComboBox
$cmbDiff.Location = New-Object System.Drawing.Point(104, 79)
$cmbDiff.Size = New-Object System.Drawing.Size(300, 22)
$cmbDiff.DropDownStyle = 'DropDownList'
$cmbDiff.DropDownWidth = 400  # the longer descriptions, in full, when open
foreach ($difficulty in $Difficulties) { [void]$cmbDiff.Items.Add($difficulty.Label) }
$grp.Controls.Add($cmbDiff)

$lblYourIp = New-Label 'Your address (tell your friend):' 34 112 380 18
$grp.Controls.Add($lblYourIp)

$txtYourIp = New-Object System.Windows.Forms.TextBox
$txtYourIp.Location = New-Object System.Drawing.Point(34, 132)
$txtYourIp.Size = New-Object System.Drawing.Size(240, 22)
$txtYourIp.ReadOnly = $true
$grp.Controls.Add($txtYourIp)

# Join-only: where to connect.
$lblAddr = New-Label "Friend's address" 34 82 110 18
$grp.Controls.Add($lblAddr)

$txtAddr = New-Object System.Windows.Forms.TextBox
$txtAddr.Location = New-Object System.Drawing.Point(144, 79)
$txtAddr.Size = New-Object System.Drawing.Size(200, 22)
$txtAddr.Text = $Settings.Address
$grp.Controls.Add($txtAddr)

$lblAddrHint = New-Label 'Just the address, for example 192.168.2.40' 144 104 270 18
$lblAddrHint.ForeColor = [System.Drawing.Color]::DimGray
$grp.Controls.Add($lblAddrHint)

# Shared
$grp.Controls.Add((New-Label 'Port' 34 156 40 18))
$txtPort = New-Object System.Windows.Forms.TextBox
$txtPort.Location = New-Object System.Drawing.Point(74, 153)
$txtPort.Size = New-Object System.Drawing.Size(70, 22)
$txtPort.Text = $Settings.Port
$grp.Controls.Add($txtPort)

$grp.Controls.Add((New-Label 'Both players must use the same port.' 154 156 260 18))
$form.Controls.Add($grp)

$lblStatus = New-Label '' 15 278 422 40
$form.Controls.Add($lblStatus)

$btnStart = New-Object System.Windows.Forms.Button
$btnStart.Text = 'Start'
$btnStart.Location = New-Object System.Drawing.Point(243, 325)
$btnStart.Size = New-Object System.Drawing.Size(100, 32)
$form.Controls.Add($btnStart)
$form.AcceptButton = $btnStart

$btnQuit = New-Object System.Windows.Forms.Button
$btnQuit.Text = 'Close'
$btnQuit.Location = New-Object System.Drawing.Point(351, 325)
$btnQuit.Size = New-Object System.Drawing.Size(86, 32)
$btnQuit.Add_Click({ $form.Close() })
$form.Controls.Add($btnQuit)

# ------------------------------------------------------------------- logic --
function Update-Mode {
    $isHost = $rbHost.Checked
    $lblDiff.Visible = $isHost
    $cmbDiff.Visible = $isHost
    $lblYourIp.Visible = $isHost
    $txtYourIp.Visible = $isHost
    $lblAddr.Visible = -not $isHost
    $txtAddr.Visible = -not $isHost
    $lblAddrHint.Visible = -not $isHost

    if ($isHost) {
        $addresses = Get-LocalAddresses
        if ($addresses.Count -gt 0) {
            $txtYourIp.Text = ($addresses -join '   ')
        } else {
            $txtYourIp.Text = 'could not detect - run ipconfig'
        }
    }
}

function Update-DirState {
    if (-not $txtDir.Text) {
        $lblDirState.Text = 'Could not find the game. Click Browse and pick PlantsVsZombies.exe.'
        $lblDirState.ForeColor = [System.Drawing.Color]::Firebrick
    } elseif (Test-Path (Join-Path $txtDir.Text 'main.pak')) {
        $lblDirState.Text = 'Found your game (main.pak is here).'
        $lblDirState.ForeColor = [System.Drawing.Color]::ForestGreen
    } else {
        $lblDirState.Text = 'No main.pak in that folder.'
        $lblDirState.ForeColor = [System.Drawing.Color]::Firebrick
    }
}

$rbHost.Add_CheckedChanged({ Update-Mode })
$txtDir.Add_TextChanged({ Update-DirState })

$btnStart.Add_Click({
    if (-not (Test-Path $ExePath)) {
        [System.Windows.Forms.MessageBox]::Show(
            "PvZ-Coop.exe is not in this folder.`r`n`r`nKeep the launcher next to the game executable.",
            'Co-op Launcher', 'OK', 'Error') | Out-Null
        return
    }
    if (-not (Test-Path (Join-Path $txtDir.Text 'main.pak'))) {
        [System.Windows.Forms.MessageBox]::Show(
            "No main.pak in that folder.`r`n`r`nClick Browse and pick PlantsVsZombies.exe in your Plants vs. Zombies folder.",
            'Co-op Launcher', 'OK', 'Error') | Out-Null
        return
    }

    $port = 0
    if (-not [int]::TryParse($txtPort.Text, [ref]$port) -or $port -lt 1 -or $port -gt 65535) {
        [System.Windows.Forms.MessageBox]::Show(
            'The port must be a number between 1 and 65535.',
            'Co-op Launcher', 'OK', 'Error') | Out-Null
        return
    }

    if (-not $rbHost.Checked -and -not $txtAddr.Text.Trim()) {
        [System.Windows.Forms.MessageBox]::Show(
            "Enter your friend's address.", 'Co-op Launcher', 'OK', 'Error') | Out-Null
        return
    }

    $commandLine = Build-GameCommandLine `
        -GameDir $txtDir.Text `
        -IsHost $rbHost.Checked `
        -Address $txtAddr.Text `
        -Port $port `
        -Difficulty $Difficulties[[Math]::Max($cmbDiff.SelectedIndex, 0)].Key

    if ($rbHost.Checked) {
        $lblStatus.Text = "Starting as host on port $port." + [Environment]::NewLine +
            'The console window waits for your friend; the game opens once they connect.'
    } else {
        $lblStatus.Text = "Connecting to $($txtAddr.Text.Trim())." + [Environment]::NewLine +
            'The game opens once the host accepts.'
    }

    $Settings.GameDir = $txtDir.Text
    $Settings.Mode = if ($rbHost.Checked) { 'host' } else { 'join' }
    $Settings.Address = $txtAddr.Text
    $Settings.Port = "$port"
    $Settings.Difficulty = $Difficulties[[Math]::Max($cmbDiff.SelectedIndex, 0)].Key
    Save-Settings

    try {
        Start-Process -FilePath $ExePath -ArgumentList $commandLine -WorkingDirectory $ScriptDir
        $form.Close()
    } catch {
        [System.Windows.Forms.MessageBox]::Show(
            "Could not start the game:`r`n$($_.Exception.Message)",
            'Co-op Launcher', 'OK', 'Error') | Out-Null
    }
})

# Restore the previous choices.
if ($Settings.Mode -eq 'join') { $rbJoin.Checked = $true } else { $rbHost.Checked = $true }
$cmbDiff.SelectedIndex = 0
for ($i = 0; $i -lt $Difficulties.Count; $i++) {
    if ($Difficulties[$i].Key -eq $Settings.Difficulty) { $cmbDiff.SelectedIndex = $i }
}
Update-Mode
Update-DirState

[void]$form.ShowDialog()
