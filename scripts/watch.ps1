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
    [string]$Replay,   # play this .ddrec file instead of a scenario
    [string]$Rig,      # rig file to watch and edit live (default: packs/rigs/humanoid16.json)
    [string]$Dll       # replay with this exact dragdoll.dll (default for SA recordings: the game's scripts\dragdoll.dll)
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$preset = 'x64-release'

# A recording replays bit-identically only through the code that made it (ADR-0007): an x86 recording (the SA game)
# opens in the x86 viewer, with the game's own DLL when it can be found.
if ($Replay) {
    $header = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Replay).Path) | Select-Object -First 512
    if ([System.Text.Encoding]::ASCII.GetString([byte[]]$header) -match '\| x86 \|') {
        $preset = 'x86-release'
        if (-not $Dll -and $env:DRAGDOLL_SA_GAME_DIR) {
            $gameDll = Join-Path $env:DRAGDOLL_SA_GAME_DIR 'scripts\dragdoll.dll'
            if (Test-Path $gameDll) { $Dll = $gameDll }
        }
        Write-Host "x86 recording: using the x86 viewer$(if ($Dll) { " with $Dll" })"
    }
}
$exe = Join-Path $repoRoot "build\$preset\bin\dragdoll_view.exe"

# A running viewer locks dragdoll.dll and would pile up windows: close any previous one first.
Get-Process dragdoll_view -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 200

if ($Build -or -not (Test-Path $exe)) {
    & (Join-Path $PSScriptRoot 'dd.ps1') $preset -Step configure
    & (Join-Path $PSScriptRoot 'dd.ps1') $preset -Step build
}

if ($List) {
    & $exe --list
    return
}

$viewerArgs = @($Scenario)
# Live rig editing: the viewer watches the repo's rig file and reloads the ragdoll whenever it is saved.
$rigFile = if ($Rig) { (Resolve-Path -LiteralPath $Rig).Path } else { Join-Path $repoRoot 'packs\rigs\humanoid16.json' }
$viewerArgs += @('--rig', "`"$rigFile`"")
if ($Fps -ne 0) { $viewerArgs += @('--fps', $Fps) }
if ($Record) { $viewerArgs += @('--record', "`"$((Resolve-Path -LiteralPath (Split-Path $Record -Parent)).Path)\$(Split-Path $Record -Leaf)`"") }
if ($Replay) { $viewerArgs += @('--replay', "`"$((Resolve-Path -LiteralPath $Replay).Path)`"") }
if ($Dll) { $viewerArgs += @('--dll', "`"$((Resolve-Path -LiteralPath $Dll).Path)`"") }
$p = Start-Process -FilePath $exe -ArgumentList $viewerArgs -WorkingDirectory (Split-Path $exe) -PassThru
Write-Host "dragdoll_view started (pid $($p.Id)): $Scenario"
