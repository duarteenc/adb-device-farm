<#
.SYNOPSIS
  Launches the farm against N devices and samples process CPU / memory / threads / handles.
.DESCRIPTION
  1. Kills any running QtScrcpy.exe
  2. `adb connect`s the first N addresses of -Hosts (default: probe the subnet for port 5555)
  3. Starts QtScrcpy.exe --farm, waits -Warmup seconds, samples every -Interval seconds for -Seconds
  4. Prints a JSON summary (and appends it to -Out if given)
  CPU% is normalised to ALL logical cores (100% = the whole machine busy).
.EXAMPLE
  .\scripts\measure-perf.ps1 -Devices 10 -Seconds 30 -Out docs\perf\baseline.jsonl
#>
param(
    [int]$Devices = 4,
    [int]$Seconds = 30,
    [int]$Warmup = 20,
    [double]$Interval = 2.0,
    [string[]]$Hosts = @(),
    [string]$Subnet = '192.168.100',
    [int]$Port = 5555,
    [string]$Out = '',
    [string]$Label = '',
    [string[]]$ExtraArgs = @(),
    [switch]$NoConnect,
    [switch]$KeepRunning,
    [switch]$Load          # drive continuous screen motion on every device (swipes in Settings)
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'output\x64\RelWithDebInfo\QtScrcpy.exe'
$adb = Join-Path (Split-Path $exe) 'adb.exe'
if (-not (Test-Path $exe)) { throw "not built: $exe" }

Get-Process QtScrcpy -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

if (-not $NoConnect) {
    if ($Hosts.Count -eq 0) {
        # Parallel TCP probe of the subnet for the ADB port.
        $probe = @()
        $tasks = @{}
        1..254 | ForEach-Object {
            $ip = "$Subnet.$_"
            $c = New-Object System.Net.Sockets.TcpClient
            $tasks[$ip] = @{ c = $c; t = $c.ConnectAsync($ip, $Port) }
        }
        Start-Sleep -Seconds 2
        foreach ($k in ($tasks.Keys | Sort-Object { [int]($_.Split('.')[-1]) })) {
            $t = $tasks[$k].t
            if ($t.IsCompleted -and -not $t.IsFaulted -and $tasks[$k].c.Connected) { $probe += $k }
            $tasks[$k].c.Dispose()
        }
        $Hosts = $probe
    }
    # Disconnect everything, then connect exactly the requested count. (start-server first:
    # a cold adb prints "daemon not running" to stderr, which -ErrorAction Stop treats as fatal.
    # Via cmd, not Start-Process -Wait: that would wait for the detached daemon forever.)
    & cmd.exe /c "`"$adb`" start-server >nul 2>&1"
    & $adb disconnect *> $null
    $targets = $Hosts | Select-Object -First $Devices
    # Plain adb.exe processes, not PowerShell jobs: a job costs a whole PowerShell start-up
    # each, which on a small machine takes longer than the connect itself and used to leave
    # most of a 20+ device set unconnected when the 20 s wait expired.
    $connectProcs = @()
    foreach ($h in $targets) { $connectProcs += Start-Process -FilePath $adb -ArgumentList @('connect', "${h}:$Port") -WindowStyle Hidden -PassThru }
    $deadline = (Get-Date).AddSeconds(15 + $targets.Count)
    do {
        Start-Sleep -Seconds 1
        $online = (& $adb devices) | Select-String "`tdevice$" | Measure-Object | Select-Object -ExpandProperty Count
    } while ($online -lt $targets.Count -and (Get-Date) -lt $deadline)
    foreach ($cp in $connectProcs) { if (-not $cp.HasExited) { $cp.Kill() } }
    Write-Host "connected $online / $($targets.Count) devices"
}

$loadProcs = @()
if ($Load) {
    # Reproducible motion: open Settings and swipe up/down for the whole run so the encoder
    # keeps producing frames (an idle home screen sends almost nothing).
    $serials = (& $adb devices) | Select-String "^(\S+)\s+device$" | ForEach-Object { $_.Matches.Groups[1].Value }
    $loops = [int](($Warmup + $Seconds + 10) / 1.2)
    $script = "am start -a android.settings.SETTINGS >/dev/null 2>&1; i=0; while [ `$i -lt $loops ]; do input swipe 540 1600 540 600 250; sleep 0.3; input swipe 540 600 540 1600 250; sleep 0.3; i=`$((i+1)); done"
    foreach ($s in $serials) {
        $loadProcs += Start-Process -FilePath $adb -ArgumentList @('-s', $s, 'shell', $script) -WindowStyle Hidden -PassThru
    }
    Write-Host "load running on $($serials.Count) devices"
}

$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $exe -ArgumentList (@('--farm') + $ExtraArgs) -WorkingDirectory (Split-Path $exe) -PassThru
Start-Sleep -Seconds 2
if ($p.HasExited) { throw "QtScrcpy exited immediately (code $($p.ExitCode))" }
Write-Host "warming up ${Warmup}s..."
Start-Sleep -Seconds $Warmup

$cores = [Environment]::ProcessorCount
$samples = @()
$prevCpu = (Get-Process -Id $p.Id).TotalProcessorTime
$prevT = Get-Date
$n = [math]::Max(1, [int]($Seconds / $Interval))
for ($i = 0; $i -lt $n; $i++) {
    Start-Sleep -Seconds $Interval
    $proc = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
    if (-not $proc) { Write-Warning 'process died during sampling'; break }
    $now = Get-Date
    $cpu = $proc.TotalProcessorTime
    $dt = ($now - $prevT).TotalSeconds
    $cpuPct = (($cpu - $prevCpu).TotalSeconds / $dt) / $cores * 100.0
    $prevCpu = $cpu; $prevT = $now
    $samples += [pscustomobject]@{
        cpu = [math]::Round($cpuPct, 1)
        ws  = [math]::Round($proc.WorkingSet64 / 1MB, 1)
        priv = [math]::Round($proc.PrivateMemorySize64 / 1MB, 1)
        threads = $proc.Threads.Count
        handles = $proc.HandleCount
    }
}
$adbProcs = @(Get-Process adb -ErrorAction SilentlyContinue).Count
$summary = [ordered]@{
    label = $Label
    devices = $Devices
    seconds = $Seconds
    cpu_avg = [math]::Round(($samples.cpu | Measure-Object -Average).Average, 1)
    cpu_max = ($samples.cpu | Measure-Object -Maximum).Maximum
    ws_mb_avg = [math]::Round(($samples.ws | Measure-Object -Average).Average, 1)
    ws_mb_max = ($samples.ws | Measure-Object -Maximum).Maximum
    priv_mb_avg = [math]::Round(($samples.priv | Measure-Object -Average).Average, 1)
    threads_avg = [math]::Round(($samples.threads | Measure-Object -Average).Average, 0)
    handles_avg = [math]::Round(($samples.handles | Measure-Object -Average).Average, 0)
    adb_processes = $adbProcs
    cores = $cores
    timestamp = (Get-Date).ToString('s')
}
$json = ($summary | ConvertTo-Json -Compress)
Write-Host $json
if ($Out) { Add-Content -Path $Out -Value $json -Encoding utf8 }
if (-not $KeepRunning) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
foreach ($lp in $loadProcs) { Stop-Process -Id $lp.Id -Force -ErrorAction SilentlyContinue }
if ($Load) {
    $serials = (& $adb devices) | Select-String "^(\S+)\s+device$" | ForEach-Object { $_.Matches.Groups[1].Value }
    foreach ($s in $serials) { Start-Process -FilePath $adb -ArgumentList @('-s', $s, 'shell', 'input keyevent KEYCODE_HOME') -WindowStyle Hidden | Out-Null }
}
