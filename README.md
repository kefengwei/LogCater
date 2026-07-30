# LogCater

Android 设备日志与文件管理桌面工具。基于 Dear ImGui + OpenGL3 构建，免安装 Android SDK 即可使用。

## 功能

- **实时 Logcat** — 流式抓取并显示 logcat 日志，支持文本/Tag/LogLevel 三维过滤，Tag 历史自动保存
- **Dropbox 查看器** — 浏览 `dumpsys dropbox` 条目，按类型筛选，支持详情弹窗和导出
- **文件浏览器** — 浏览设备存储目录，包名选择器，文件预览（支持 logcat/UE4 日志高亮），上传/下载，书签收藏
- **应用信息** — 查看已安装应用的包名、版本号、版本名、Target SDK
- **PID→进程名映射** — 日志行自动关联进程名
- **全局缩放** — Ctrl+滚轮 / Ctrl+0 重置 / Ctrl+= / Ctrl+- 缩放 UI

## 下载

从 [Releases](../../releases) 页面下载最新 `LogCater.zip`，解压即用（已内置 adb）。

## 构建

### 依赖

- Visual Studio 2022+ (MSVC)
- CMake 3.21+
- Git

依赖库通过 CMake FetchContent 自动下载：

- [GLFW](https://github.com/glfw/glfw) 3.4
- [Dear ImGui](https://github.com/ocornut/imgui) v1.91.9
- [nlohmann/json](https://github.com/nlohmann/json) v3.11.3

### 编译

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

或直接运行 `go.bat`（自动调用 vcvars64）。

## 快捷键

| 快捷键 | 功能 |
|---|---|
| Ctrl + 滚轮 | 缩放 UI |
| Ctrl + = | 放大 |
| Ctrl + - | 缩小 |
| Ctrl + 0 | 重置缩放 |

## 许可

MIT License
