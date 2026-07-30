# LogCater

Android 设备日志与文件管理桌面工具。基于 Dear ImGui + OpenGL3 构建。免安装 Android SDK — 已内置 ADB。

## 功能

- **实时 Logcat** — 流式抓取并显示 logcat 日志，支持文本 / Tag / Level 三维过滤，Tag 历史自动保存
- **Dropbox 查看器** — 浏览 `dumpsys dropbox` 条目，按类型筛选，详情弹窗，导出到文件
- **文件浏览器** — 浏览设备存储，包名选择器，文件预览（支持 logcat/UE4 日志高亮），上传/下载，目录书签
- **应用信息** — 查看已安装应用详情：包名、版本名、版本号、Target SDK
- **进程名映射** — 日志行自动关联显示进程名
- **全局 UI 缩放** — Ctrl+滚轮 / Ctrl+0 / Ctrl+= / Ctrl+- 缩放界面，缩放比例持久保存
- **自动更新检查** — 启动时自动检查 GitHub Releases 是否有新版本

## 下载

从 [Releases](../../releases) 页面下载最新 `LogCater.zip`，解压即用（已内置 ADB）。

> ⚠️ **Windows Defender 误报说明**
>
> LogCater **完全开源，不含任何恶意代码**。由于未进行代码签名，Windows Defender 可能将本程序标记为可疑。
>
> **解决方法：**
> - 在 Windows Defender 中将 `logcater.exe` 添加为排除项
> - 或[自行编译](#构建)（无需签名，不会被标记）

## 构建

### 环境要求

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
| Ctrl + 滚轮上 | 放大 UI |
| Ctrl + 滚轮下 | 缩小 UI |
| Ctrl + = | 放大 UI |
| Ctrl + - | 缩小 UI |
| Ctrl + 0 | 重置缩放 |

## 许可

MIT License
