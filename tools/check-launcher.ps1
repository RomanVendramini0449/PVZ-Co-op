# Static checks for the launcher script.
#
# Exists because a syntax check is not enough: an earlier edit deleted a
# function while leaving its call site in place, which parses perfectly and
# only fails when a user presses the button. This walks the syntax tree and
# verifies every command the script invokes actually resolves.

param(
    [string]$Path = (Join-Path $PSScriptRoot 'windows/PVZ-Coop-Launcher.ps1')
)

$failed = $false

if (-not (Test-Path $Path)) {
    Write-Host "FAIL: $Path not found"
    exit 1
}

# --- syntax ---
$errors = $null
$tokens = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$errors)
if ($errors -and $errors.Count -gt 0) {
    Write-Host "FAIL: $($errors.Count) syntax error(s)"
    foreach ($e in $errors) {
        Write-Host ("  line {0}: {1}" -f $e.Extent.StartLineNumber, $e.Message)
    }
    exit 1
}
Write-Host "OK   syntax"

# --- every command the script calls must resolve ---
$defined = @{}
foreach ($fn in $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true)) {
    $defined[$fn.Name] = $true
}

# Cmdlets that only exist on Windows, so they cannot be resolved from the
# Linux CI runner this check also runs on.
$windowsOnly = @('Get-NetIPAddress')

$missing = @()
foreach ($cmd in $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.CommandAst] }, $true)) {
    $name = $cmd.GetCommandName()
    if (-not $name) { continue }
    if ($defined.ContainsKey($name)) { continue }
    if ($windowsOnly -contains $name) { continue }
    if (Get-Command $name -ErrorAction SilentlyContinue) { continue }
    $missing += ("line {0}: {1}" -f $cmd.Extent.StartLineNumber, $name)
}

if ($missing.Count -gt 0) {
    Write-Host "FAIL: $($missing.Count) call(s) to something that does not exist"
    $missing | ForEach-Object { Write-Host "  $_" }
    $failed = $true
} else {
    Write-Host "OK   every command resolves ($($defined.Count) functions defined)"
}

# --- the quoting helper must survive, since losing it breaks every launch ---
if (-not $defined.ContainsKey('ConvertTo-QuotedArg')) {
    Write-Host "FAIL: ConvertTo-QuotedArg is gone; paths with spaces would break"
    $failed = $true
} else {
    Write-Host "OK   ConvertTo-QuotedArg present"
}

if ($failed) { exit 1 }
Write-Host "`nlauncher checks passed"
