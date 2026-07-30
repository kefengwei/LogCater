# LogCater

Android device log & file management desktop tool. Built with Dear ImGui + OpenGL3. No Android SDK required — ADB is bundled.

## Features

- **Real-time Logcat** — Stream and display logcat output with text/tag/level filters. Tag history saved automatically.
- **Dropbox Viewer** — Browse `dumpsys dropbox` entries, filter by type, detail popup, export to file.
- **File Browser** — Browse device storage, package selector, file preview (logcat/UE4 log highlighting), upload/download, directory bookmarks.
- **App Info** — View installed app details: package name, version name, version code, target SDK.
- **Process Name Mapping** — PID-to-process-name resolution displayed inline.
- **Global UI Zoom** — Ctrl+MouseWheel / Ctrl+0 / Ctrl+= / Ctrl+- to scale the UI. Persisted across sessions.
- **Auto Update Check** — Checks GitHub Releases for new versions on startup.

## Download

Download the latest `LogCater.zip` from [Releases](../../releases), extract and run. ADB is included.

> ⚠️ **Windows Defender False Positive**
>
> LogCater is **open-source and contains no malicious code**. Because it is not code-signed, Windows Defender may flag it as suspicious.
>
> **Solutions:**
> - Add `logcater.exe` to Windows Defender exclusions
> - Or [build from source](#build) yourself (no signing needed, won't be flagged)

## Build

### Prerequisites

- Visual Studio 2022+ (MSVC)
- CMake 3.21+
- Git

Dependencies are fetched automatically via CMake FetchContent:

- [GLFW](https://github.com/glfw/glfw) 3.4
- [Dear ImGui](https://github.com/ocornut/imgui) v1.91.9
- [nlohmann/json](https://github.com/nlohmann/json) v3.11.3

### Compile

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Or run `go.bat` (automatically calls vcvars64).

## Shortcuts

| Shortcut | Action |
|---|---|
| Ctrl + Scroll Up | Zoom in |
| Ctrl + Scroll Down | Zoom out |
| Ctrl + = | Zoom in |
| Ctrl + - | Zoom out |
| Ctrl + 0 | Reset zoom to 100% |

## License

MIT License
