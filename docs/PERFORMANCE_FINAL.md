# Performance — final (`feat/ultimate-device-farm`)

Same machine, same harness and same load as [`PERFORMANCE_BASELINE.md`](PERFORMANCE_BASELINE.md)
(unmodified `main` @ `c1808c2`). Every number below was measured on 2026-08-24 on the
real farm; nothing is extrapolated. Where a metric has no baseline counterpart it is
marked **after-only**; where it was not captured it says **NOT MEASURED**.

## Test machine and farm

Intel Core i5-3230M (2 cores / 4 threads, 2012), 16 GB, Intel HD 4000, Windows 10 Pro
19045, Wi-Fi 192.168.100.180/24 · 78 × Samsung Galaxy S8 (Android 9) on TCP :5555.
Build: MSVC 14.51 (VS 18), Qt 6.11.1, RelWithDebInfo.

## Method

`scripts\measure-perf.ps1 -Devices N -Load -Warmup (20+2N) -Seconds 30 -ExtraArgs @('--data-dir', <bench dir>)`

Identical to the baseline: the harness `adb connect`s exactly N phones, starts a
*Settings* swipe loop on each (`input swipe`, so the encoder keeps producing frames),
launches `QtScrcpy.exe --farm`, waits for warm-up and samples CPU (normalised to the
whole 4-thread machine), working set, private bytes, threads and handles every 2 s
for 30 s. Two harness changes were needed and are committed (they only affect the
connect step, not the measurement): the connect stage uses plain `adb.exe` processes
instead of PowerShell jobs (20+ jobs did not finish in the 20 s window on this CPU),
and `adb start-server` runs first through `cmd` so a cold server cannot abort the script.

Two data directories isolate the bench from the operator's settings:

| dir | purpose | settings |
| --- | --- | --- |
| `benchdata` | like-for-like with the baseline | maxSize 800, 30 fps, 4 Mbps, adaptive quality **off**, 4 concurrent starts, discovery/auto-connect **off** (the harness decides which N phones exist) |
| `benchdata78` | product defaults on the whole farm | preset *balanced* 720 px / 30 fps / 3 Mbps with adaptive quality **on** (>50 devices → 360 px / 10 fps / 600 kbps), discovery off |

Raw rows: [`perf/final.jsonl`](perf/final.jsonl) (baseline: [`perf/baseline.jsonl`](perf/baseline.jsonl)).

## Results — like-for-like (baseline profile, motion on every screen)

| Devices | Baseline CPU avg / max | **Final CPU avg / max** | Baseline WS | **Final WS** | Baseline threads | **Final threads** | Baseline mirrored | **Final mirrored** |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 3.7 % / 4.9 % | **3.4 % / 5.2 %** | 89.0 MB | **119.1 MB** | 16 | **18** | 1 | **1** |
| 5 | 8.6 % / 10.9 % | **9.2 % / 13.2 %** | 126.1 MB | **155.8 MB** | 20 | **22** | 5 | **5** |
| 10 | 14.1 % / 17.1 % | **16.6 % / 19.6 %** | 161.1 MB | **199.0 MB** | 23 | **27** | 10 | **10** |
| 20 | 9.8 % / 12.4 % ¹ | **30.7 % / 33.7 %** | 135.5 MB ¹ | **263.1 MB** | 19 ¹ | **35** | ≈10 ¹ | **20 / 20** |
| 78 | not possible ² | **19.1 % / 27.6 %** (adaptive) | — | **313.4 MB** | — | **92** | — | **78 / 78** |

¹ The baseline's 20-device row is *not* 20 devices: only ≈10 ever reached the
mirroring state (see the baseline document), which is why its CPU is lower than its
own 10-device row. The final build mirrors all 20 (49 `adb.exe` helpers alive, 35
threads) and is the only valid 20-device measurement.
² The baseline has no discovery and stalls its connect queue on the first slow phone;
78 simultaneous sessions were never reachable with it.

### How to read this honestly

* **Per-stream cost did not go down** in the like-for-like profile: ≈1.5 % of the
  machine per 800 px / 30 fps stream in both builds. Software H.264 decoding (FFmpeg)
  dominates on this 2012 CPU; the render patch (one `makeCurrent` per frame instead of
  one per YUV plane) trims GUI-thread time but is not visible in whole-process CPU at
  ≤10 devices. The "200 %" target was **not** met as *CPU per device*.
* **Capacity is where the target was exceeded**: 20/20 at the baseline profile and
  78/78 with the adaptive profile at 19 % CPU — 7.8× the number of simultaneously
  mirrored devices at ≈1.4× the CPU of the baseline's best sustainable run (10
  devices). Adaptive quality (360 px / 10 fps above 50 devices) is what makes 78 fit;
  the operator can pin any tile or group to a higher profile.
* **Memory**: base footprint is ≈30 MB higher (SQLite, eleven pages, tray, perf
  monitor, OCR runtime); the per-device increment is unchanged at ≈7–8 MB (lazy GL
  widgets: a tile allocates its OpenGL surface only when the first frame arrives).
* `adb_processes` in the raw rows counts the harness's own per-device `adb shell`
  swipe loops (78 of the 156 in the 78-device row); the farm itself keeps one
  bounded executor (`adb/concurrency`, default 8) plus the server.

## After-only measurements (no baseline instrumentation existed)

Captured on the same farm during the v3 verification sessions with the built-in
Performance page / diagnostics export (numbers are what the page showed; they were
not re-sampled by the harness):

| Metric | Value | Where it comes from |
| --- | --- | --- |
| Full `/24` discovery sweep (254 hosts, 64 parallel probes, 800 ms timeout) | ≈2.5 s | `DeviceDiscoveryService` log line `scan finished` |
| Connect + mirror the whole farm from a cold start (78 devices, adaptive) | ≈61 s to 78/78 Mirroring | Activity page timestamps |
| GUI event-loop lag while 78 phones connect | < 50 ms after the async `readInfo` core patch (1.0–1.5 s stalls before it) | Performance page lag probe |
| Idle steady state, 78 devices mirroring (home screens, no load) | 10.7–13.5 % CPU, 270–316 MB WS | Performance page |
| Keep-awake apply + verify, 78 devices | 78 applied, 0 failures | Activity page |
| Unit tests | 11 / 11 pass (`scripts\test.bat`) | ctest |
| Decoded / rendered / dropped FPS per tile, decode→display latency, ADB round-trip | shown live on the Performance page and in the diagnostics export | **NOT RECORDED** as a table in this document |

## Long-run stability

`scripts\longrun-test.ps1 -Minutes 12 -IntervalSeconds 30 -ChurnSeconds 120` on the
real farm with the product defaults (discovery + auto-connect + auto-mirror on, adaptive
quality): every 120 s the harness `adb disconnect`s a random phone and the farm has to
bring it back. Raw samples: [`perf/longrun.jsonl`](perf/longrun.jsonl).

| | start (all 78 up, 11:46:56) | end (11:57:27) | Δ |
| --- | ---: | ---: | ---: |
| Private bytes | 328.8 MB | 334.6 MB | **+5.8 MB** over 10.5 min |
| Working set | 316.1 MB | 262.1 MB | −54 MB (the OS trimmed the working set at 11:56; private bytes did not move) |
| Threads | 93 | 93 | 0 |
| Handles | 2438 | 2435 | −3 |
| CPU (avg of 24 samples) | | 9.2 % | idle home screens |
| Reconnect churns | | 6 / 6 recovered | e.g. `.108`: dropped 11:47:26 → retry 1 s, 2 s → ADB Online 11:47:29 → keep-awake re-applied → Mirroring 11:47:31 |
| Process alive at the end | | yes | |

The whole-run summary row in the jsonl reports `ws_growth_mb: 66` because its first
sample (11:45:55) was taken while sessions were still starting (39 threads, 31 adb
helpers); the table above compares the first sample after all 78 sessions were up.
No growth in private bytes, threads or handles across six reconnect cycles → no
leak in the reconnect / keep-awake / health paths over this window. A multi-hour run
was **NOT MEASURED**.

## Mock devices (no ADB)

`QtScrcpy.exe --farm --mock-devices N --data-dir <fresh dir>` (synthetic frames, no
adb, separate `MockDeviceProvider`; the mock devices follow Auto Mirror, so every tile
streams), sampled for 40 s after warm-up on the same machine. Raw rows:
[`perf/mock.jsonl`](perf/mock.jsonl); screenshot: [`perf/mock-300-devices.png`](perf/mock-300-devices.png).

| Mock devices | Synthetic stream | CPU avg / max | Working set | Threads | UI lag (status bar) |
| ---: | --- | ---: | ---: | ---: | ---: |
| 100 | 360 px, 10 fps each | 7.9 % / 9.0 % | 181 MB | 14 | 18 ms |
| 300 | 180 px, 3 fps each (auto above 100) | 3.5 % / 4.1 % | 207 MB | 13 | 17 ms |

Two things this test caught on the way (both fixed in `perf(ui)`, both relevant to real
farms): an off-screen tile used to allocate its OpenGL widget on its first throttled
frame (100 streams: 220 → 181 MB once only viewport tiles own a surface), and four
pages rebuilt a whole table/combo on *every* registry signal — 300 devices coming
online meant 300 × 300-row rebuilds and a GUI thread frozen for minutes; those
rebuilds are now coalesced to one per 150 ms. Mock streams are synthesised on the GUI
thread, so their CPU is not comparable with real decode; the test is about the
registry, grid, pages and render path at 4× the physical farm.
