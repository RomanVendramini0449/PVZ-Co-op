# PVZ Co-op Launcher
#
# A small window for starting a co-op session without typing command lines.
# Launched by PVZ-Coop-Launcher.bat, which sits next to it.
#
# It only ever builds a command line and starts pvz-portable.exe with it.
# Nothing here touches your Plants vs. Zombies install.

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

# $PSScriptRoot is the reliable one; the fallback covers odd invocations.
$ScriptDir = $PSScriptRoot
if (-not $ScriptDir) { $ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition }
if (-not $ScriptDir) { $ScriptDir = (Get-Location).Path }
$ExePath   = Join-Path $ScriptDir 'pvz-portable.exe'
$ConfigPath = Join-Path $ScriptDir 'coop-launcher-settings.txt'

# ---------------------------------------------------------------- settings --
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

function Find-GameDir {
    # Whatever worked last time wins, so a deliberate choice is never
    # overridden by detection.
    if ($Settings.GameDir -and (Test-Path (Join-Path $Settings.GameDir 'main.pak'))) {
        return $Settings.GameDir
    }
    $candidates = Get-GameCandidates
    if ($candidates.Count -gt 0) { return $candidates[0] }
    return ''
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
    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = 'Pick the folder containing main.pak'
    if ($txtDir.Text -and (Test-Path $txtDir.Text)) { $dlg.SelectedPath = $txtDir.Text }
    if ($dlg.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { $txtDir.Text = $dlg.SelectedPath }
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
$cmbDiff.Size = New-Object System.Drawing.Size(150, 22)
$cmbDiff.DropDownStyle = 'DropDownList'
[void]$cmbDiff.Items.Add('Normal')
[void]$cmbDiff.Items.Add('Hard  (half sun, tougher zombies)')
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
        $lblDirState.Text = 'Pick the folder that contains main.pak.'
        $lblDirState.ForeColor = [System.Drawing.Color]::Firebrick
    } elseif (Test-Path (Join-Path $txtDir.Text 'main.pak')) {
        $lblDirState.Text = 'Found main.pak.'
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
            "pvz-portable.exe is not in this folder.`r`n`r`nKeep the launcher next to the game executable.",
            'Co-op Launcher', 'OK', 'Error') | Out-Null
        return
    }
    if (-not (Test-Path (Join-Path $txtDir.Text 'main.pak'))) {
        [System.Windows.Forms.MessageBox]::Show(
            "No main.pak in that folder.`r`n`r`nPick your Plants vs. Zombies install folder.",
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

    $argList = @('-resdir', $txtDir.Text)

    if ($rbHost.Checked) {
        $argList += '--host'
        $argList += '--port'; $argList += "$port"
        if ($cmbDiff.SelectedIndex -eq 1) { $argList += '--difficulty'; $argList += 'hard' }
        $lblStatus.Text = "Starting as host on port $port." + [Environment]::NewLine +
            'The console window waits for your friend; the game opens once they connect.'
    } else {
        $address = $txtAddr.Text.Trim()
        if (-not $address) {
            [System.Windows.Forms.MessageBox]::Show(
                "Enter your friend's address.", 'Co-op Launcher', 'OK', 'Error') | Out-Null
            return
        }
        # Accept a bare address or one that already carries a port.
        if ($address -notmatch ':') { $address = "${address}:${port}" }
        $argList += '--join'; $argList += $address
        $lblStatus.Text = "Connecting to $address." + [Environment]::NewLine +
            'The game opens once the host accepts.'
    }

    $Settings.GameDir = $txtDir.Text
    $Settings.Mode = if ($rbHost.Checked) { 'host' } else { 'join' }
    $Settings.Address = $txtAddr.Text
    $Settings.Port = "$port"
    $Settings.Difficulty = if ($cmbDiff.SelectedIndex -eq 1) { 'hard' } else { 'normal' }
    Save-Settings

    $commandLine = ($argList | ForEach-Object { ConvertTo-QuotedArg $_ }) -join ' '

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
$cmbDiff.SelectedIndex = if ($Settings.Difficulty -eq 'hard') { 1 } else { 0 }
Update-Mode
Update-DirState

[void]$form.ShowDialog()
