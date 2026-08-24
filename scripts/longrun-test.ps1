<#
.SYNOPSIS
  Long-run stability test: starts the farm (real devices or --mock-devices N), samples
  working set / private bytes / handles / threads / CPU every -IntervalSeconds for
  -Minutes, and reports growth. Optionally injects reconnect churn (adb disconnect /
  reconnect of a random device) every -ChurnSeconds to exercise the reconnect loop.
.EXAMPLE
  .\scripts\longrun-test.ps1 -Minutes 30 -IntervalSeconds 30 -ChurnSeconds 120 -Out docs\perf\longrun.jsonl
  .\scripts\longrun-test.ps1 -Minutes 10 -Mock 200
#>
param(
    [int]$Minutes = 30,
    [int]$IntervalSeconds = 30,
    [int]$ChurnSeconds = 0,
    [int]$Mock = 0,
    [string]$Out = '',
    [string[]]$ExtraArgs = @()
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'output\x64\RelWithDebInfo\QtScrcpy.exe'
$adb = Join-Path (Split-Path $exe) 'adb.exe'
if (-not (Test-Path $exe)) { throw "not built: $exe" }
Get-Process QtScrcpy -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
$argList = @('--farm') + $ExtraArgs
if ($Mock -gt 0) { $argList += @('--mock-devices', "$Mock") }
$p = Start-Process -FilePath $exe -ArgumentList $argList -WorkingDirectory (Split-Path $exe) -PassThru
Start-Sleep -Seconds 5
if ($p.HasExited) { throw "QtScrcpy exited immediately (code $($p.ExitCode))" }
$cores = [Environment]::ProcessorCount
$samples = @()
$prevCpu = (Get-Process -Id $p.Id).TotalProcessorTime
$prevT = Get-Date
$deadline = (Get-Date).AddMinutes($Minutes)
$lastChurn = Get-Date
$churns = 0
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds $IntervalSeconds
    $proc = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
    if (-not $proc) { Write-Warning "process died at $(Get-Date -Format s)"; break }
    $now = Get-Date
    $cpu = $proc.TotalProcessorTime
    $cpuPct = (($cpu - $prevCpu).TotalSeconds / ($now - $prevT).TotalSeconds) / $cores * 100.0
    $prevCpu = $cpu; $prevT = $now
    $s = [pscustomobject]@{
        t = $now.ToString('s')
        cpu = [math]::Round($cpuPct, 1)
        ws_mb = [math]::Round($proc.WorkingSet64 / 1MB, 1)
        priv_mb = [math]::Round($proc.PrivateMemorySize64 / 1MB, 1)
        handles = $proc.HandleCount
        threads = $proc.Threads.Count
        adb_procs = @(Get-Process adb -ErrorAction SilentlyContinue).Count
        churns = $churns
    }
    $samples += $s
    Write-Host ("{0}  cpu {1,5}%  ws {2,7} MB  priv {3,7} MB  handles {4,5}  threads {5,3}  adb {6}" -f $s.t, $s.cpu, $s.ws_mb, $s.priv_mb, $s.handles, $s.threads, $s.adb_procs)
    if ($Out) { Add-Content -Path $Out -Value ($s | ConvertTo-Json -Compress) -Encoding utf8 }
    if ($ChurnSeconds -gt 0 -and $Mock -eq 0 -and ($now - $lastChurn).TotalSeconds -ge $ChurnSeconds) {
        # Reconnect churn: drop one online TCP device from adb; the farm must bring it back.
        $online = (& $adb devices) | Select-String "^(\d+\.\d+\.\d+\.\d+:\d+)\s+device$" | ForEach-Object { $_.Matches.Groups[1].Value }
        if ($online.Count -gt 0) {
            $victim = $online | Get-Random
            & $adb disconnect $victim | Out-Null
            $churns++
            Write-Host "  churn: disconnected $victim (farm should reconnect it)"
        }
        $lastChurn = $now
    }
}
if ($samples.Count -ge 2) {
    $first = $samples[0]; $last = $samples[-1]
    $summary = [ordered]@{
        minutes = $Minutes
        samples = $samples.Count
        ws_start_mb = $first.ws_mb; ws_end_mb = $last.ws_mb; ws_growth_mb = [math]::Round($last.ws_mb - $first.ws_mb, 1)
        priv_start_mb = $first.priv_mb; priv_end_mb = $last.priv_mb; priv_growth_mb = [math]::Round($last.priv_mb - $first.priv_mb, 1)
        handles_start = $first.handles; handles_end = $last.handles
        threads_start = $first.threads; threads_end = $last.threads
        cpu_avg = [math]::Round(($samples.cpu | Measure-Object -Average).Average, 1)
        churns = $churns
        alive = -not (Get-Process -Id $p.Id -ErrorAction SilentlyContinue).HasExited
    }
    $json = $summary | ConvertTo-Json -Compress
    Write-Host $json
    if ($Out) { Add-Content -Path $Out -Value $json -Encoding utf8 }
}
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
