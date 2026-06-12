# v2.0 Architecture — device-farm extension over QtScrcpyCore

Goal: view & control **many** Android devices at once with the **lowest possible
latency**, for educational debugging. We reuse QtScrcpy's native core untouched and
build a farm-oriented UI on top.

## The pipeline (why it's low-latency)

```
device screen
  → scrcpy-server (on-device, MediaCodec H.264)         [QtScrcpyCore/src/server, third_party]
  → ADB reverse tunnel  (useReverse=true)               [QtScrcpyCore/src/adb]
  → demuxer → FFmpeg H.264 decode (C++)                 [QtScrcpyCore/src/device/decoder, demuxer]
  → DeviceObserver::onFrame(Y,U,V planes)               [include/QtScrcpyCore.h]
  → QYUVOpenGLWidget uploads YUV → GPU shader            [QtScrcpy/render/qyuvopenglwidget.*]
```

No format conversion on the CPU, no extra copies, no browser. Decoded YUV goes
straight to an OpenGL texture.

## The core API we build on (`namespace qsc`, `include/QtScrcpyCore.h`)

- **`IDeviceManage::getInstance()`** — `connectDevice(DeviceParams)`, `disconnectDevice(serial)`,
  `disconnectAllDevice()`, `getDevice(serial)`. Emits `deviceConnected(...)` / `deviceDisconnected(...)`.
- **`IDevice`** — per device: `registerDeviceObserver(DeviceObserver*)`, input
  (`mouseEvent`/`wheelEvent`/`keyEvent`), and actions (`postGoHome`, `postPower`,
  `pushFileRequest`, `installApkRequest`, `screenshot`, ...).
- **`DeviceObserver::onFrame(w,h,Y,U,V,strideY,strideU,strideV)`** — the decoded-frame hook.
- **`DeviceParams`** (`QtScrcpyCoreDef.h`) — all connection/encoding options (see knobs below).

## Low-latency knobs (set per `DeviceParams`)

| Field                 | Default   | Farm low-latency setting | Why |
| --------------------- | --------- | ------------------------ | --- |
| `renderExpiredFrames` | `false`   | **keep `false`**         | Drops late frames instead of showing stale ones — the single biggest latency lever. |
| `maxFps`              | `0` (∞)   | `60` (or `30` for 20+ devices) | Caps producer rate so we never queue frames we can't display. |
| `maxSize`             | `720`     | `1024` for 1–2 devices; `480–540` per tile in a big grid | Less pixels = less to encode/ship/decode. Scale to tile size. |
| `bitRate`             | `2 Mbps`  | `4–8 Mbps` focused; `1–2 Mbps` per grid tile | Sharper vs. less to ship over the link. |
| `useReverse`          | `true`    | `true`                   | `adb reverse` tunnel; falls back to `forward`. |
| `codecOptions`        | `""`      | try `"low-latency=1"` *(encoder-dependent)* | Some MediaCodec encoders expose a low-latency mode. |
| `closeScreen`         | `false`   | optional `true`          | Saves device battery / privacy in a lab. |

We'll ship 2–3 **presets** ("Focus / lowest latency", "Classroom grid", "Many devices")
that just populate `DeviceParams`.

## UI plan — reuse vs. build

| Upstream piece | v2.0 use |
| --- | --- |
| `render/qyuvopenglwidget.*` | **Reuse as-is** — the per-tile renderer. |
| `ui/videoform.*` | Reuse as the **focused single-device** view (full input + toolbar). |
| `groupcontroller/groupcontroller.*` | **Reuse/extend** — broadcast synchronized input to selected devices ("control all"). |
| `ui/dialog.*` | Replace its single-window launching with a **farm dashboard**. |
| **NEW `farm/`** | The grid: a `QGridLayout`/`QOpenGLWidget` wall of lightweight `DeviceTile`s. |

### New components (`QtScrcpy/farm/`)

- **`DeviceTile`** — owns one `QYUVOpenGLWidget` + a `DeviceObserver` impl; registered on
  an `IDevice`. Shows live frame; click to focus; small status chip (serial, fps).
- **`FarmGridView`** — tiles N devices in a near-square grid; lazy: only render visible tiles.
- **`FarmController`** — wraps `IDeviceManage`: batch connect (USB serials + TCP endpoints),
  "mirror all", "stop all", apply a latency preset to all.
- **`FocusOverlay`** — promote one tile to a full `videoform` for precise control, while the
  rest keep mirroring (optionally throttled to a lower fps to save resources).

## Connection (USB + WiFi/TCP)

- **USB**: enumerate via the core's adb wrapper (`adbprocess.h` / `src/adb`), one `DeviceParams`
  per serial.
- **WiFi/TCP**: `adb tcpip 5555` on a USB device, read its `wlan0` IP, then connect to
  `ip:5555` — same `DeviceParams` flow, serial is the `ip:port`.

## Implementation phases

1. **Toolchain + baseline** — build unmodified upstream (see `BUILD_WINDOWS.md`). Confirm a
   single device mirrors with low latency. *(blocked on Qt6/MSVC/CMake install)*
2. **FarmController + DeviceTile** — connect 2+ devices programmatically, render each in a tile.
3. **FarmGridView dashboard** — replace the launcher; batch connect, mirror-all, presets.
4. **Focus + group control** — click-to-focus full control; wire `groupcontroller` for
   synchronized input across selected devices.
5. **Educational polish** — spotlight a device to a projector window, per-tile labels/names,
   "freeze/thaw" a tile, screenshot-all.

## Open questions for later

- Per-tile fps throttling policy when many devices are connected (visible vs. off-screen).
- Whether to keep upstream's `audio/` path (we default to no audio for latency).
- Naming/identifying devices in a classroom (map serial → student/seat label).
