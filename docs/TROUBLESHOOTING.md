# Troubleshooting

Start with **Performance › Export diagnostics bundle** — it contains the
rotating logs (`%APPDATA%\ADBDeviceFarm\logs\adb-device-farm.log`), the
metrics snapshot, the device table and the settings (secrets removed).

## Build / start

| Symptom | Fix |
| --- | --- |
| `Could not find a package configuration file provided by "Qt6"` | run `scripts\check-environment.ps1`; install the Qt MSVC kit with `scripts\bootstrap.ps1` (downloads the official archives) or set `QT_ROOT` |
| `cl.exe is not recognized` | install Visual Studio "Desktop development with C++"; `build.bat` finds it through `vswhere` |
| `LNK1168: cannot open QtScrcpy.exe` | the app is still running — `Get-Process QtScrcpy \| Stop-Process -Force` |
| app exits immediately | Qt DLLs missing: `windeployqt output\x64\RelWithDebInfo\QtScrcpy.exe` (`run-farm.ps1` does it) |
| "database schema is newer than this build" | you ran a newer version on the same data directory; the file is left untouched — update the build or use `--data-dir` |

## Devices

| Symptom | Fix |
| --- | --- |
| nothing appears, LAN scan finds 0 hosts | check Settings › Discovery › Subnet matches the PC's network (`ipconfig`); phones must have *ADB over network* on port 5555; Windows Firewall may prompt for `adb.exe` — allow private networks |
| device shows **Unauthorized** ⚠ | approve the RSA prompt on the phone (this cannot be automated); the farm retries every 2 min |
| device flips Offline/Reconnecting | WiFi drop or adb TCP reset; the backoff loop (1…60 s) reconnects. `adb kill-server` on the PC also causes this once |
| mirror fails with "scrcpy server failed to start" | old `scrcpy-server` on the phone, or the port lease collided with another process — restart mirror; check `logs` for `scid`/`port` |
| taps land on the wrong place on some models | keep *Normalise resolution/density* ON (Settings › Mirroring) or use *Aspect-aware coordinates* in host mode |
| keep-awake `Failed: … rejected` | vendor ROM ignores `settings put`; the health check still wakes the display with `KEYCODE_WAKEUP` |
| `Awake but locked` | the phone has a lock screen; the farm never bypasses it — remove the PIN on owned test devices |

## Performance

| Symptom | Fix |
| --- | --- |
| high CPU with many mirrors | choose the *Performance* preset (360p/15fps), enable *Adaptive quality*, lower *off-screen fps* (Settings › Performance) |
| UI lag (status bar shows `UI lag > 200 ms`) | check Performance page: decoded vs rendered fps and the ADB queue; reduce simultaneous mirror starts |
| `adb` commands time out | raise Settings › ADB › timeout; a wedged `adb.exe` is killed by the watchdog automatically |

## Automation

| Symptom | Fix |
| --- | --- |
| *OCR not available* | Windows › Settings › Time & Language › add a language and its *Basic typing* feature |
| *Find image* never matches | crop the template from a device screenshot at the same resolution; lower the threshold to 0.7–0.8; restrict the region |
| workflow stuck | every node has a timeout; use *Stop* on the run — devices finish their current step and the run ends as Cancelled |
| error screenshot missing | Settings › Automation › *Capture a screenshot when a step fails* |

## Logs

`logs/adb-device-farm.log` (rotated at 5 MB × 5 files). Lines are
`time [thread] [level] [component] [device] message`. Components: `app`,
`adb`, `discovery`, `device`, `keepawake`, `health`, `automation`,
`scheduler`, `storage`, `registry`. An unclean previous shutdown is reported at
startup in the Activity page; configuration is never reset because of a crash.
