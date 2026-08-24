<#
.SYNOPSIS
  One-step release build: portable folder + zip (scripts\package-portable.ps1) and the
  Windows installer (installer\adb-device-farm.iss compiled with Inno Setup 6).
  Output: dist\ADBDeviceFarm-portable-<ver>.zip and dist\ADBDeviceFarm-setup-<ver>.exe
.PARAMETER SkipPortable  Reuse the existing dist\ADBDeviceFarm-portable-<ver> folder.
.NOTES
  Inno Setup is free: winget install -e --id JRSoftware.InnoSetup
#>
param([switch]$SkipPortable)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $SkipPortable) {
    & (Join-Path $PSScriptRoot 'package-portable.ps1')
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw "package-portable failed ($LASTEXITCODE)" }
}
$iscc = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue; if ($cmd) { $iscc = $cmd.Source } }
if (-not $iscc) { throw 'Inno Setup 6 not found (winget install -e --id JRSoftware.InnoSetup)' }
Write-Host "compiling installer with $iscc"
& $iscc /Q (Join-Path $repo 'installer\adb-device-farm.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }
$setup = Get-ChildItem (Join-Path $repo 'dist') -Filter 'ADBDeviceFarm-setup-*.exe' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Write-Host "installer: $($setup.FullName) ($([math]::Round($setup.Length / 1MB, 1)) MB)"
