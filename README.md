# ADB Device Farm — v2.0

**Low-latency, multi-device Android mirroring & control for educational debugging.**

A native **C++ / Qt 6** application built as a fork of
[QtScrcpy](https://github.com/barry-ran/QtScrcpy) (Apache-2.0). The goal of v2.0 is
the lowest possible latency when viewing and controlling **many** Android devices at
once — for classroom / lab debugging — by keeping the whole pipeline native:

```
Android device → scrcpy-server (MediaCodec H.264) → ADB stream
   → FFmpeg decode (C++ core) → YUV frame → OpenGL upload → on-screen
```

No browser, no Electron, no extra copies: decode lands in a `QYUVOpenGLWidget` that
uploads YUV planes straight to the GPU.

## Why fork QtScrcpy instead of writing from scratch

QtScrcpy already separates the latency-critical core from the UI:

- **`QtScrcpy/QtScrcpyCore/`** — the C++ core (FFmpeg decode, ADB transport, device
  control, the `scrcpy-server`). Exposes a clean `qsc::` API:
  `IDeviceManage`, `IDevice`, `DeviceObserver`.
- **`QtScrcpy/`** — the Qt UI (windows, OpenGL render, input), plus a
  **`groupcontroller/`** that already broadcasts synchronized input to several devices.

v2.0 reuses the core untouched and replaces/extends the UI with a **device-farm grid**.
See [`docs/ARCHITECTURE_V2.md`](docs/ARCHITECTURE_V2.md).

## Status

- [x] Forked & vendored QtScrcpy as the v2.0 base (see [`UPSTREAM.md`](UPSTREAM.md))
- [x] Toolchain validated — upstream builds & runs locally (Qt 6.11.1 + MSVC + Ninja).
      Build with `scripts\build.bat`; see [`docs/BUILD_WINDOWS.md`](docs/BUILD_WINDOWS.md)
- [ ] Device-farm grid view (N × `QYUVOpenGLWidget`)
- [ ] Batch connect (USB + WiFi/TCP) and "mirror all"
- [ ] Educational features (focus/spotlight a device, low-latency presets)

> Build note: a one-line compat fix was needed for Qt 6.11 — `QtScrcpy/CMakeLists.txt`
> now requests the `GuiPrivate` component explicitly. After building, run `windeployqt`
> once (see build doc) to copy Qt DLLs next to the exe.

## Build

See **[`docs/BUILD_WINDOWS.md`](docs/BUILD_WINDOWS.md)**. First milestone: build the
**unmodified** upstream to validate your toolchain, then we extend.

## License

Apache License 2.0 — see [`LICENSE`](LICENSE). Derived from QtScrcpy and scrcpy;
attribution in [`UPSTREAM.md`](UPSTREAM.md).
