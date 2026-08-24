# Third-party components

| Component | License | Use | Notes |
| --- | --- | --- | --- |
| [QtScrcpy](https://github.com/barry-ran/QtScrcpy) / QtScrcpyCore | Apache-2.0 | vendored core (decode, ADB transport, control) | see `UPSTREAM.md`; local changes documented there |
| [scrcpy](https://github.com/Genymobile/scrcpy) server | Apache-2.0 | `scrcpy-server` pushed to devices | bundled in `QtScrcpyCore/src/third_party` |
| Qt 6 (Core, Gui, Widgets, Network, Multimedia, OpenGL, OpenGLWidgets, Sql, Concurrent, Test) | LGPL-3.0 / GPL / commercial | UI, networking, SQLite driver, threading, tests | dynamically linked; Qt DLLs may be redistributed under LGPL-3.0 with the license text |
| SQLite (via Qt `QSQLITE` plugin) | Public domain | device registry, workflows, schedules, job history | |
| FFmpeg (avcodec/avformat/avutil/swscale/swresample, LGPL build) | LGPL-2.1+ | H.264 decoding | vendored MSVC import libs + DLLs in `QtScrcpyCore/src/third_party/ffmpeg` |
| Android platform-tools (`adb.exe`, AdbWinApi) | Apache-2.0 | device communication | bundled in `QtScrcpyCore/src/third_party/adb/win` |
| [lunasvg](https://github.com/sammycage/lunasvg) (+ plutovg) | MIT | rendering the numbered-wallpaper SVG template | git submodule `third_party/lunasvg` |
| Windows OCR (`Windows.Media.Ocr`, C++/WinRT) | part of Windows 10/11 | local text recognition for *Wait for text* / *Tap text* | no download; uses the language packs installed on the PC |
| Ollama / OpenAI-compatible endpoints | — | **optional** natural-language workflow generation | disabled by default; the product never depends on it |

No component requires a subscription. Tesseract and OpenCV are **not** linked;
`OcrProvider` and `ImageMatcher` have compile-time hooks (`FARM_HAVE_TESSERACT`,
`FARM_HAVE_OPENCV`) if a packager wants them.

Build tools (not redistributed): MSVC (Visual Studio Community/Build Tools),
CMake, Ninja, Python + `py7zr` (only for `scripts/install-qt.py`).
