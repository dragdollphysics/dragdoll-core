<#
.SYNOPSIS
    Opens the live DragDoll viewer on a scenario (detached, without stealing keyboard focus).

.EXAMPLE
    ./scripts/watch.ps1 car_vs_pole_120kmh
    ./scripts/watch.ps1 shove_mid_fall -Record build/shove.ddrec
    ./scripts/watch.ps1 -Replay build/shove.ddrec
    ./scripts/watch.ps1 -List
    ./scripts/watch.ps1 pile_zup -Build      # rebuild first
#>
param(
    [Parameter(Position = 0)]
    [string]$Scenario = 'car_vs_pole_120kmh',
    [switch]$Build,
    [switch]$List,
    [ValidateSet(0, 30, 60, 144)]
    [int]$Fps = 0,
    [string]$Record,   # record the run to this .ddrec file (F9 adds a note)
    [string]$Replay    # play this .ddrec file instead of a scenario
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repoRoot 'build\x64-release\bin\dragdoll_view.exe'

# A running viewer locks dragdoll.dll and would pile up windows: close any previous one first.
Get-Process dragdoll_view -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 200

if ($Build -or -not (Test-Path $exe)) {
    & (Join-Path $PSScriptRoot 'dd.ps1') x64-release -Step configure
    & (Join-Path $PSScriptRoot 'dd.ps1') x64-release -Step build
}

if ($List) {
    & $exe --list
    return
}

$viewerArgs = @($Scenario)
if ($Fps -ne 0) { $viewerArgs += @('--fps', $Fps) }
if ($Record) { $viewerArgs += @('--record', "`"$((Resolve-Path -LiteralPath (Split-Path $Record -Parent)).Path)\$(Split-Path $Record -Leaf)`"") }
if ($Replay) { $viewerArgs += @('--replay', "`"$((Resolve-Path -LiteralPath $Replay).Path)`"") }
$p = Start-Process -FilePath $exe -ArgumentList $viewerArgs -WorkingDirectory (Split-Path $exe) -PassThru
Write-Host "dragdoll_view started (pid $($p.Id)): $Scenario"
