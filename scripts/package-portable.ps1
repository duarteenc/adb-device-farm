<#
.SYNOPSIS
  Builds a portable, self-contained folder (and .zip) of the ADB Device Farm:
  QtScrcpy.exe + Qt DLLs + FFmpeg + adb + scrcpy-server + plugins + a `portable` marker
  so settings/database/logs live next to the executable.
.PARAMETER Out   Output directory (default dist\).
.PARAMETER Name  Package name (default ADBDeviceFarm-portable-<version>).
#>
param([string]$Out = '', [string]$Name = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $repo 'output\x64\RelWithDebInfo'
$exe = Join-Path $bin 'QtScrcpy.exe'
if (-not (Test-Path $exe)) { throw "not built: $exe (run scripts\build.bat)" }
$qt = $env:QT_ROOT; if (-not $qt) { $qt = 'C:\Qt\6.11.1\msvc2022_64' }
$version = (Get-Content (Join-Path $repo 'QtScrcpy\appversion') -Raw).Trim()
if (-not $version -or $version -eq '0.0.0') { $version = '3.0.0' }
if (-not $Out) { $Out = Join-Path $repo 'dist' }
if (-not $Name) { $Name = "ADBDeviceFarm-portable-$version" }
$dest = Join-Path $Out $Name
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
New-Item -ItemType Directory -Force $dest | Out-Null

# 1. deploy Qt into a clean folder (windeployqt copies plugins/tls/sqldrivers/etc.)
Copy-Item $exe $dest
& "$qt\bin\windeployqt.exe" --no-translations --release (Join-Path $dest 'QtScrcpy.exe') | Out-Null
# 2. runtime files produced by the build (FFmpeg, adb, scrcpy-server, helper apk/bat)
foreach ($f in 'avcodec-58.dll','avformat-58.dll','avutil-56.dll','swscale-5.dll','swresample-3.dll','adb.exe','AdbWinApi.dll','AdbWinUsbApi.dll','scrcpy-server','sndcpy.apk','sndcpy.bat') {
    $p = Join-Path $bin $f
    if (Test-Path $p) { Copy-Item $p $dest }
}
foreach ($opt in '333FarmerWallpaperHelper.apk','wallpaper_template.svg') {
    $p = Join-Path $bin $opt
    if (Test-Path $p) { Copy-Item $p $dest }
}
# 3. config + keymaps used by the upstream single-device UI (relative paths in main.cpp)
New-Item -ItemType Directory -Force (Join-Path $dest 'config') | Out-Null
Copy-Item (Join-Path $repo 'config\config.ini') (Join-Path $dest 'config\config.ini')
Copy-Item -Recurse (Join-Path $repo 'keymap') (Join-Path $dest 'keymap')
# 4. portable marker + docs + licenses
Set-Content -Path (Join-Path $dest 'portable') -Value 'settings, database and logs are stored in .\data' -Encoding ascii
Copy-Item (Join-Path $repo 'LICENSE') (Join-Path $dest 'LICENSE.txt')
New-Item -ItemType Directory -Force (Join-Path $dest 'docs') | Out-Null
foreach ($d in 'README.md','docs\AUTOMATION.md','docs\DEVICE_DISCOVERY.md','docs\KEEP_AWAKE.md','docs\TROUBLESHOOTING.md','docs\THIRD_PARTY.md') {
    $p = Join-Path $repo $d
    if (Test-Path $p) { Copy-Item $p (Join-Path $dest 'docs') }
}
# 5. launcher
Set-Content -Path (Join-Path $dest 'ADB Device Farm.cmd') -Value "@echo off`r`nstart `"`" `"%~dp0QtScrcpy.exe`" --farm`r`n" -Encoding ascii
# 6. zip
$zip = "$dest.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $dest -DestinationPath $zip
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "portable package: $dest"
Write-Host "zip: $zip ($size MB)"
