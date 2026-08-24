<#
.SYNOPSIS
  Verifies the ADB Device Farm development toolchain and prints PASS/FAIL per component.
.DESCRIPTION
  Detects (never assumes) the locations of Git, GitHub CLI, MSVC, Qt 6 MSVC kit, CMake, Ninja,
  ADB, the vendored FFmpeg libraries and OpenGL support. Exit code is the number of FAILED
  required components (optional ones are reported as WARN and never fail the check).
  Run:  .\scripts\check-environment.ps1 [-Json]
#>
[CmdletBinding()]
param([switch]$Json)

$ErrorActionPreference = 'SilentlyContinue'
$repo = Split-Path -Parent $PSScriptRoot
$results = New-Object System.Collections.Generic.List[object]

function Add-Result([string]$Name, [bool]$Ok, [string]$Detail, [bool]$Required = $true) {
    $status = if ($Ok) { 'PASS' } elseif ($Required) { 'FAIL' } else { 'WARN' }
    $results.Add([pscustomobject]@{ Component = $Name; Status = $status; Detail = $Detail; Required = $Required })
}

function Find-Exe([string]$Name, [string[]]$Candidates) {
    foreach ($c in $Candidates) { if ($c -and (Test-Path $c)) { return (Resolve-Path $c).Path } }
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# ---- Git ----
$git = Find-Exe 'git.exe' @("$env:ProgramFiles\Git\cmd\git.exe")
if ($git) { Add-Result 'Git' $true ((& $git --version 2>$null) + " @ $git") } else { Add-Result 'Git' $false 'not found - winget install Git.Git' }

# ---- GitHub CLI (optional: only needed to open the PR) ----
$gh = Find-Exe 'gh.exe' @("$env:ProgramFiles\GitHub CLI\gh.exe", "$env:LOCALAPPDATA\Programs\GitHub CLI\gh.exe")
if ($gh) { Add-Result 'GitHub CLI' $true (((& $gh --version 2>$null) | Select-Object -First 1) + " @ $gh") $false }
else { Add-Result 'GitHub CLI' $false 'not found - winget install GitHub.cli (optional, for PRs)' $false }

# ---- MSVC ----
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
}
if (-not $vsPath) {
    foreach ($p in @("$env:ProgramFiles\Microsoft Visual Studio\18\Community", "$env:ProgramFiles\Microsoft Visual Studio\2022\Community",
                     "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools", "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools")) {
        if (Test-Path "$p\VC\Auxiliary\Build\vcvars64.bat") { $vsPath = $p; break }
    }
}
if ($vsPath -and (Test-Path "$vsPath\VC\Auxiliary\Build\vcvars64.bat")) {
    $msvcVer = Get-ChildItem "$vsPath\VC\Tools\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1
    Add-Result 'MSVC' $true "toolset $($msvcVer.Name) @ $vsPath"
} else { Add-Result 'MSVC' $false 'no Visual Studio with C++ x64 tools - install "Desktop development with C++"' }

# ---- Qt ----
$qtDir = $null
if ($env:QT_ROOT -and (Test-Path "$env:QT_ROOT\lib\cmake\Qt6\Qt6Config.cmake")) { $qtDir = $env:QT_ROOT }
if (-not $qtDir -and (Test-Path 'C:\Qt\6.11.1\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake')) { $qtDir = 'C:\Qt\6.11.1\msvc2022_64' }
if (-not $qtDir) {
    Get-ChildItem 'C:\Qt' -Directory -Filter '6.*' -ErrorAction SilentlyContinue | ForEach-Object {
        Get-ChildItem $_.FullName -Directory -Filter 'msvc*_64' | ForEach-Object {
            if (-not $qtDir -and (Test-Path "$($_.FullName)\lib\cmake\Qt6\Qt6Config.cmake")) { $qtDir = $_.FullName }
        }
    }
}
if ($qtDir) {
    $qtVer = ((& "$qtDir\bin\qmake.exe" -v 2>$null) | Select-String 'Qt version ([\d.]+)').Matches.Groups[1].Value
    Add-Result 'Qt' $true "Qt $qtVer @ $qtDir"
    $missing = @()
    foreach ($m in 'Qt6Core','Qt6Widgets','Qt6Network','Qt6Multimedia','Qt6OpenGL','Qt6OpenGLWidgets','Qt6Sql','Qt6Concurrent','Qt6Test') {
        if (-not (Test-Path "$qtDir\lib\cmake\$m\${m}Config.cmake")) { $missing += $m }
    }
    if ($missing.Count -eq 0) { Add-Result 'Qt MSVC kit' $true 'Core Widgets Network Multimedia OpenGL OpenGLWidgets Sql Concurrent Test' }
    else { Add-Result 'Qt MSVC kit' $false ("missing modules: " + ($missing -join ', ') + ' - run scripts\bootstrap.ps1') }
} else {
    Add-Result 'Qt' $false 'Qt 6 MSVC kit not found - run scripts\bootstrap.ps1 (downloads official Qt archives)'
    Add-Result 'Qt MSVC kit' $false 'n/a'
}

# ---- CMake ----
$cmake = Find-Exe 'cmake.exe' @('C:\Qt\Tools\CMake_64\bin\cmake.exe', "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
if ($cmake) { Add-Result 'CMake' $true (((& $cmake --version) | Select-Object -First 1) + " @ $cmake") } else { Add-Result 'CMake' $false 'not found - winget install Kitware.CMake' }

# ---- Ninja ----
$ninja = Find-Exe 'ninja.exe' @('C:\Qt\Tools\Ninja\ninja.exe', "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe")
if ($ninja) { Add-Result 'Ninja' $true ("ninja " + (& $ninja --version) + " @ $ninja") } else { Add-Result 'Ninja' $false 'not found - winget install Ninja-build.Ninja' }

# ---- ADB ----
$adb = Find-Exe 'adb.exe' @('C:\platform-tools\adb.exe', "${env:ProgramFiles(x86)}\Android\android-sdk\platform-tools\adb.exe",
                            "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe", "$repo\QtScrcpy\QtScrcpyCore\src\third_party\adb\win\adb.exe")
if ($adb) { Add-Result 'ADB' $true ((((& $adb version 2>$null) | Select-Object -First 2) -join ' ') + " @ $adb") } else { Add-Result 'ADB' $false 'not found (the repo bundles one under QtScrcpyCore/src/third_party/adb/win)' }

# ---- FFmpeg (vendored) ----
$ff = "$repo\QtScrcpy\QtScrcpyCore\src\third_party\ffmpeg"
$ffOk = (Test-Path "$ff\lib\x64\avcodec.lib") -and (Test-Path "$ff\bin\x64\avcodec-58.dll") -and (Test-Path "$ff\include\libavcodec\avcodec.h")
Add-Result 'FFmpeg project libs' $ffOk $(if ($ffOk) { "$ff (lib/x64 + bin/x64 + include)" } else { 'vendored FFmpeg missing - did the clone complete?' })

# ---- lunasvg submodule ----
$luna = Test-Path "$repo\third_party\lunasvg\CMakeLists.txt"
Add-Result 'lunasvg submodule' $luna $(if ($luna) { 'initialized' } else { 'run: git submodule update --init --recursive' })

# ---- OpenGL ----
$gpu = Get-CimInstance Win32_VideoController | Select-Object -First 1
$glOk = $null -ne $gpu
Add-Result 'OpenGL' $glOk $(if ($gpu) { "$($gpu.Name) (driver $($gpu.DriverVersion)); Qt falls back to opengl32sw.dll if no HW GL 2.0" } else { 'no display adapter detected' }) $false

# ---- Report ----
if ($Json) { $results | ConvertTo-Json }
else {
    Write-Host ''
    Write-Host 'ADB Device Farm - environment check' -ForegroundColor Cyan
    Write-Host ('=' * 60)
    foreach ($r in $results) {
        $color = switch ($r.Status) { 'PASS' { 'Green' } 'FAIL' { 'Red' } default { 'Yellow' } }
        Write-Host ('{0,-22} ' -f $r.Component) -NoNewline
        Write-Host ('{0,-5}' -f $r.Status) -ForegroundColor $color -NoNewline
        Write-Host "  $($r.Detail)"
    }
    Write-Host ('=' * 60)
}
$fails = @($results | Where-Object { $_.Status -eq 'FAIL' }).Count
if ($fails -eq 0) { Write-Host 'All required components present. Build with: scripts\build.bat' -ForegroundColor Green }
else { Write-Host "$fails required component(s) missing. Run scripts\bootstrap.ps1 to install what can be automated." -ForegroundColor Red }
exit $fails
