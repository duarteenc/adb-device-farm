<#
.SYNOPSIS
  Launches the ADB Device Farm dashboard (QtScrcpy.exe --farm) from the build output.
.PARAMETER Config   Build configuration folder (default RelWithDebInfo).
.PARAMETER Wait     Block until the process exits and return its exit code.
.PARAMETER ExtraArgs  Extra command-line arguments (e.g. '--mock-devices','100').
#>
param([string]$Config = 'RelWithDebInfo', [switch]$Wait, [string[]]$ExtraArgs = @())
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo "output\x64\$Config\QtScrcpy.exe"
if (-not (Test-Path $exe)) { Write-Error "Not built: $exe - run scripts\build.bat first"; exit 1 }
# Qt runtime DLLs are deployed by windeployqt (once, after a clean output/).
if (-not (Test-Path (Join-Path (Split-Path $exe) 'Qt6Core.dll'))) {
    $qt = $env:QT_ROOT; if (-not $qt) { $qt = 'C:\Qt\6.11.1\msvc2022_64' }
    if (Test-Path "$qt\bin\windeployqt.exe") { & "$qt\bin\windeployqt.exe" --no-translations $exe | Out-Null }
}
Get-Process QtScrcpy -ErrorAction SilentlyContinue | Stop-Process -Force
$argList = @('--farm') + $ExtraArgs
$p = Start-Process -FilePath $exe -ArgumentList $argList -WorkingDirectory (Split-Path $exe) -PassThru
if ($Wait) { $p.WaitForExit(); exit $p.ExitCode }
Start-Sleep -Seconds 3
if ($p.HasExited) { Write-Error "QtScrcpy exited immediately (code $($p.ExitCode)) - missing DLL or crash"; exit 1 }
Write-Host "Farm running (PID $($p.Id))."
