<#
.SYNOPSIS
    DragDoll build helper: loads the Visual Studio developer environment for the preset's
    architecture, then configures, builds and/or tests with CMake presets.

.EXAMPLE
    ./scripts/dd.ps1 x64-debug              # configure + build + test
    ./scripts/dd.ps1 x86-release -Step build
    ./scripts/dd.ps1 all                     # every dev preset (x64/x86, debug/release)
#>
param(
    [Parameter(Position = 0, Mandatory = $true)]
    [ValidateSet('x64-debug', 'x64-release', 'x86-debug', 'x86-release', 'x86-ship', 'all')]
    [string]$Preset,

    [ValidateSet('all', 'configure', 'build', 'test')]
    [string]$Step = 'all'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

function Get-VsInstallPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found: install Visual Studio (Build Tools) with the C++ workload.' }
    $path = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $path) { throw 'No Visual Studio installation with the C++ x86/x64 tools found.' }
    return $path
}

function Invoke-InVsEnv([string]$arch, [string]$command) {
    $vcvars = Join-Path (Get-VsInstallPath) 'VC\Auxiliary\Build\vcvarsall.bat'
    # vcvarsall looks for vswhere on PATH; make it findable to avoid a harmless warning.
    $installerDir = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
    # Run vcvarsall and the command in the same cmd session so the environment applies.
    cmd /c "set `"PATH=$installerDir;%PATH%`" && `"$vcvars`" $arch >nul && $command"
    if ($LASTEXITCODE -ne 0) { throw "Command failed (exit $LASTEXITCODE): $command" }
}

function Invoke-Preset([string]$name) {
    $arch = $name.Split('-')[0]   # x64 / x86 -> vcvarsall argument
    Push-Location $repoRoot
    try {
        Write-Host "=== $name ===" -ForegroundColor Cyan
        if ($Step -in 'all', 'configure') { Invoke-InVsEnv $arch "cmake --preset $name" }
        if ($Step -in 'all', 'build')     { Invoke-InVsEnv $arch "cmake --build --preset $name" }
        if ($Step -in 'all', 'test')      { Invoke-InVsEnv $arch "ctest --preset $name" }
    }
    finally { Pop-Location }
}

if ($Preset -eq 'all') {
    foreach ($p in 'x64-debug', 'x64-release', 'x86-debug', 'x86-release') { Invoke-Preset $p }
}
else {
    Invoke-Preset $Preset
}
