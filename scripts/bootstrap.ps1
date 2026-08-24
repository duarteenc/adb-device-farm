<#
.SYNOPSIS
  Installs the free build dependencies of ADB Device Farm that can be automated, then re-runs
  the environment check.
.DESCRIPTION
  Uses only official channels:
    * winget (Microsoft's package manager) for Git, GitHub CLI, CMake, Ninja, Android platform-tools
    * download.qt.io (the Qt Online Installer's own repository) for the Qt 6 MSVC kit,
      via scripts\install-qt.py (SHA-256 verified)
  Visual Studio / MSVC cannot be installed unattended in a reliable way; the script tells you
  what to install if it's missing. Re-run scripts\check-environment.ps1 afterwards.
.PARAMETER QtVersion  Qt version to install when missing (default 6.11.1).
.PARAMETER SkipQt     Do not attempt the Qt download.
#>
param([string]$QtVersion = '6.11.1', [switch]$SkipQt)
$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot

function Have([string]$exe) { $null -ne (Get-Command $exe -ErrorAction SilentlyContinue) }
function Winget-Install([string]$id, [string]$label) {
    if (-not (Have 'winget')) { Write-Warning "winget not available - install $label manually"; return }
    Write-Host "Installing $label via winget ($id)..." -ForegroundColor Cyan
    winget install --id $id -e --accept-package-agreements --accept-source-agreements --disable-interactivity | Out-Null
}

# ---- Git / GitHub CLI ----
if (-not (Have 'git'))  { Winget-Install 'Git.Git' 'Git' }
if (-not (Have 'gh') -and -not (Test-Path "$env:ProgramFiles\GitHub CLI\gh.exe")) { Winget-Install 'GitHub.cli' 'GitHub CLI' }

# ---- Visual Studio (manual) ----
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = $null
if (Test-Path $vswhere) { $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null }
if (-not $vs) {
    Write-Warning 'MSVC x64 toolset not found. Install Visual Studio Build Tools with "Desktop development with C++":'
    Write-Warning '  winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"'
}

# ---- CMake / Ninja (VS bundles both; only install if neither is present) ----
$vsCMake = if ($vs) { "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" } else { '' }
$vsNinja = if ($vs) { "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" } else { '' }
if (-not (Have 'cmake') -and -not (Test-Path 'C:\Qt\Tools\CMake_64\bin\cmake.exe') -and -not (Test-Path $vsCMake)) { Winget-Install 'Kitware.CMake' 'CMake' }
if (-not (Have 'ninja') -and -not (Test-Path 'C:\Qt\Tools\Ninja\ninja.exe') -and -not (Test-Path $vsNinja)) { Winget-Install 'Ninja-build.Ninja' 'Ninja' }

# ---- ADB (optional: the repo bundles adb.exe; a system copy is handy for scripts) ----
if (-not (Have 'adb') -and -not (Test-Path 'C:\platform-tools\adb.exe') -and -not (Test-Path "${env:ProgramFiles(x86)}\Android\android-sdk\platform-tools\adb.exe")) {
    Winget-Install 'Google.PlatformTools' 'Android platform-tools'
}

# ---- Qt 6 MSVC kit ----
$qtOk = (Test-Path "C:\Qt\$QtVersion\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake") -or ($env:QT_ROOT -and (Test-Path "$env:QT_ROOT\lib\cmake\Qt6\Qt6Config.cmake"))
if (-not $qtOk -and -not $SkipQt) {
    if (-not (Have 'python')) { Winget-Install 'Python.Python.3.12' 'Python' }
    if (Have 'python') {
        python -m pip install --quiet py7zr
        python "$repo\scripts\install-qt.py" --version $QtVersion --out 'C:\Qt'
    } else {
        Write-Warning "Python not available - install Qt $QtVersion MSVC 2022 64-bit with the Qt Online Installer (https://www.qt.io/download-qt-installer) including Qt Multimedia."
    }
}

& "$repo\scripts\check-environment.ps1"
