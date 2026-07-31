#include "HelpPanel.h"
#include "imgui.h"

void HelpPanel::render() {
    // Language toggle
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 160);
    if (ImGui::RadioButton("EN", m_lang == 0)) m_lang = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("中文", m_lang == 1)) m_lang = 1;
    ImGui::Separator();

    const bool cn = (m_lang == 1);

    // Scrollable content area
    ImGui::BeginChild("##helpScroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // --- 概述 / Overview ---
    if (cn) {
        ImGui::TextWrapped(
            "LogCater 是一款通过 ADB 管理 Android 设备的桌面工具。"
            "通过 USB 连接设备（需开启 USB 调试），LogCater 会自动检测设备。"
        );
    } else {
        ImGui::TextWrapped(
            "LogCater is a desktop tool for managing Android devices via ADB. "
            "Connect your device via USB (with USB Debugging enabled), and LogCater "
            "will automatically detect it."
        );
    }

    // --- 快速开始 / Getting Started ---
    if (ImGui::CollapsingHeader(cn ? "快速开始" : "Getting Started", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (cn) {
            ImGui::BulletText("在 Android 设备上开启 USB 调试（开发者选项）。");
            ImGui::BulletText("用 USB 数据线将设备连接到电脑。");
            ImGui::BulletText("在菜单栏的设备下拉列表中选择你的设备。");
            ImGui::BulletText("LogCater 内置 adb，无需单独安装 Android SDK。");
            ImGui::BulletText("如果设备未被检测到，请检查 USB 调试是否已授权。");
        } else {
            ImGui::BulletText("Enable USB Debugging on your Android device (Developer Options).");
            ImGui::BulletText("Connect the device to your PC via USB cable.");
            ImGui::BulletText("Select your device from the dropdown in the menu bar.");
            ImGui::BulletText("LogCater bundles adb internally — no Android SDK installation needed.");
            ImGui::BulletText("If your device isn't detected, check that USB Debugging is authorized.");
        }
    }

    // --- WiFi ADB 配对 / WiFi ADB Pairing ---
    if (ImGui::CollapsingHeader(cn ? "WiFi 无线连接 (Android 11+)" : "WiFi ADB Pairing (Android 11+)")) {
        if (cn) {
            ImGui::TextWrapped("通过 WiFi 无线连接设备，无需 USB 数据线。需要 Android 11 及以上版本。");
            ImGui::BulletText("点击菜单栏的 \"WiFi\" 按钮打开配对窗口。");
            ImGui::BulletText("方式一（推荐）：在 Android 设备上：设置 > 开发者选项 > 无线调试 > 使用二维码配对设备。");
            ImGui::BulletText("用设备扫描 LogCater 显示的二维码，LogCater 自动完成配对和连接。");
            ImGui::BulletText("方式二（兼容性更好）：在设备上选择 \"使用配对码配对设备\"，将显示的 IP 地址:端口 和 6 位配对码输入 LogCater 的 \"Pair Code\" 页签，点击 \"Start Pairing\"。");
            ImGui::BulletText("配对码模式下连接端口可留空自动发现，或在设备无线调试主界面查看后手动填写。");
            ImGui::BulletText("设备连接成功后将在下拉列表中显示，之后无需 USB 即可使用 logcat、文件浏览等功能。");
            ImGui::BulletText("确保设备和电脑连接到同一个 WiFi 网络。");
            ImGui::BulletText("提示：某些公共/访客网络会隔离设备通信（AP Isolation），如配对失败请尝试使用手机热点。");
        } else {
            ImGui::TextWrapped("Connect your device wirelessly — no USB cable needed. Requires Android 11+.");
            ImGui::BulletText("Click the \"WiFi\" button in the menu bar to open the pairing window.");
            ImGui::BulletText("Method 1 (recommended): On your Android device: Settings > Developer options > Wireless debugging > Pair device with QR code.");
            ImGui::BulletText("Scan the QR code shown in LogCater — it auto-discovers, pairs, and connects.");
            ImGui::BulletText("Method 2 (more compatible): choose \"Pair device with pairing code\" on the device, then enter the IP:port and 6-digit code into the \"Pair Code\" tab in LogCater and click \"Start Pairing\".");
            ImGui::BulletText("In pair-code mode the connect port can be left empty (auto-detected), or filled in from the Wireless debugging main screen.");
            ImGui::BulletText("Once connected the device appears in the dropdown — logcat, file browser, etc. work without USB.");
            ImGui::BulletText("Make sure your device and PC are on the same WiFi network.");
            ImGui::BulletText("Tip: Public/guest networks may have AP Isolation that blocks mDNS. Try a phone hotspot if pairing fails.");
        }
    }

    // --- Logcat 标签页 ---
    if (ImGui::CollapsingHeader(cn ? "Logcat 标签页" : "Logcat Tab")) {
        if (cn) {
            ImGui::TextWrapped("实时抓取并显示已连接设备的 Android logcat 输出。");
            ImGui::BulletText("文本过滤 — 按关键词过滤日志（不区分大小写）。");
            ImGui::BulletText("Tag 过滤 — 按日志 Tag 过滤（如 \"ActivityManager\"）。Tag 历史自动保存。");
            ImGui::BulletText("Level 过滤 — 勾选/取消日志级别（Verbose, Debug, Info, Warning, Error, Fatal）。");
            ImGui::BulletText("暂停/继续 — 冻结日志流以便查看，不会丢失新数据。");
            ImGui::BulletText("清除 — 丢弃所有已缓冲的日志条目。");
            ImGui::BulletText("自动滚动 — 自动跟随新日志行。向上滚动时自动暂停，点击 \"Go Bottom\" 恢复。");
            ImGui::BulletText("点击日志行可在侧边面板查看详情（时间戳、PID、TID、完整消息）。");
            ImGui::BulletText("PID→进程名映射自动解析运行中的应用。");
        } else {
            ImGui::TextWrapped("Real-time streaming of Android logcat output from the connected device.");
            ImGui::BulletText("Text Filter — filter logs by keyword (case-insensitive).");
            ImGui::BulletText("Tag Filter — filter by log tag (e.g. \"ActivityManager\"). Tag history is saved automatically.");
            ImGui::BulletText("Level Filter — check/uncheck levels (Verbose, Debug, Info, Warning, Error, Fatal).");
            ImGui::BulletText("Pause/Resume — freeze the log stream to inspect entries without new data arriving.");
            ImGui::BulletText("Clear — discard all buffered log entries.");
            ImGui::BulletText("Auto-scroll — automatically follows new log lines. Disabled when you scroll up; click \"Go Bottom\" to resume.");
            ImGui::BulletText("Click a log line to view details (timestamps, PID, TID, full message) in the side panel.");
            ImGui::BulletText("PID-to-process-name mapping is resolved automatically for running apps.");
        }
    }

    // --- Dropbox 标签页 ---
    if (ImGui::CollapsingHeader(cn ? "Dropbox 标签页" : "Dropbox Tab")) {
        if (cn) {
            ImGui::TextWrapped("查看 Android 系统 dropbox 条目（崩溃、ANR、WTF 报告），通过 'adb shell dumpsys dropbox' 获取。");
            ImGui::BulletText("点击 \"Fetch Entries\" 加载可用的 dropbox 条目列表。");
            ImGui::BulletText("使用下拉菜单按类型筛选（如 data_app_crash, system_server_wtf）。");
            ImGui::BulletText("双击条目可在弹窗中查看完整内容。");
            ImGui::BulletText("导出 — 将条目内容保存到电脑上的文本文件。");
        } else {
            ImGui::TextWrapped("View Android system dropbox entries (crashes, ANRs, WTF reports) via 'adb shell dumpsys dropbox'.");
            ImGui::BulletText("Click \"Fetch Entries\" to load the list of available dropbox entries.");
            ImGui::BulletText("Filter by type using the dropdown (e.g. data_app_crash, system_server_wtf).");
            ImGui::BulletText("Double-click an entry to view its full content in a popup window.");
            ImGui::BulletText("Export — save entry content to a text file on your PC.");
        }
    }

    // --- Files 标签页 ---
    if (ImGui::CollapsingHeader(cn ? "Files 标签页" : "Files Tab")) {
        if (cn) {
            ImGui::TextWrapped("浏览 Android 设备文件系统，查看和传输文件。");
            ImGui::BulletText("包名选择器 — 选择一个应用包名以浏览其数据目录。");
            ImGui::BulletText("快速导航按钮：/data/data/、/sdcard/ 等常用位置。");
            ImGui::BulletText("双击 .log 或 .txt 文件可预览内容（支持语法高亮）。");
            ImGui::BulletText("UE4/Unreal Engine 日志语法高亮支持。");
            ImGui::BulletText("下载 — 将文件从设备拉取到电脑。");
            ImGui::BulletText("上传 — 从 Windows 资源管理器拖放文件到表格即可推送到设备。");
            ImGui::BulletText("书签 — 收藏常用目录以便快速访问，跨会话保存。");
        } else {
            ImGui::TextWrapped("Browse the Android device file system, view and transfer files.");
            ImGui::BulletText("Package selector — choose an app package to browse its data directory.");
            ImGui::BulletText("Quick-nav buttons for common locations: /data/data/, /sdcard/, etc.");
            ImGui::BulletText("Double-click a .log or .txt file to preview its contents with syntax highlighting.");
            ImGui::BulletText("UE4/Unreal Engine log syntax highlighting is supported for .log files.");
            ImGui::BulletText("Download — pull a file from the device to your PC.");
            ImGui::BulletText("Upload — drag & drop a file from Windows Explorer onto the table to push it to the device.");
            ImGui::BulletText("Bookmarks — star frequently-used directories for quick access. Saved across sessions.");
        }
    }

    // --- Apps 标签页 ---
    if (ImGui::CollapsingHeader(cn ? "Apps 标签页" : "Apps Tab")) {
        if (cn) {
            ImGui::TextWrapped("查看设备上已安装应用的详细信息。");
            ImGui::BulletText("来源筛选 — 切换第三方应用、系统应用、全部应用。");
            ImGui::BulletText("显示：应用名称、包名、版本名、版本号、Target SDK。");
            ImGui::BulletText("点击行可复制包名到剪贴板。");
            ImGui::BulletText("Refresh — 从设备重新加载应用列表。");
            ImGui::BulletText("文本过滤 — 按包名或应用名搜索。");
        } else {
            ImGui::TextWrapped("View detailed information about installed applications on the device.");
            ImGui::BulletText("Source filter — switch between Third-party, System, and All apps.");
            ImGui::BulletText("Shows: App Name, Package Name, Version Name, Version Code, Target SDK.");
            ImGui::BulletText("Click a row to copy the package name to the clipboard.");
            ImGui::BulletText("Refresh — reload the app list from the device.");
            ImGui::BulletText("Text filter — search by package name or app name.");
        }
    }

    // --- 快捷键 ---
    if (ImGui::CollapsingHeader(cn ? "快捷键" : "Keyboard Shortcuts")) {
        if (ImGui::BeginTable("ShortcutsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn(cn ? "快捷键" : "Shortcut");
            ImGui::TableSetupColumn(cn ? "功能" : "Action");
            ImGui::TableHeadersRow();

            auto row = [](const char* key, const char* action) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(key);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(action);
            };

            if (cn) {
                row("Ctrl + 滚轮上", "放大 UI");
                row("Ctrl + 滚轮下", "缩小 UI");
                row("Ctrl + =", "放大 UI");
                row("Ctrl + -", "缩小 UI");
                row("Ctrl + 0", "重置缩放至 100%");
            } else {
                row("Ctrl + MouseWheel Up", "Zoom in");
                row("Ctrl + MouseWheel Down", "Zoom out");
                row("Ctrl + =", "Zoom in");
                row("Ctrl + -", "Zoom out");
                row("Ctrl + 0", "Reset zoom to 100%");
            }

            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (cn) {
            ImGui::TextWrapped("UI 缩放比例会自动保存，下次启动时恢复。");
        } else {
            ImGui::TextWrapped("UI scale is saved automatically and restored on next launch.");
        }
    }

    // --- 更新检查 ---
    if (ImGui::CollapsingHeader(cn ? "更新检查" : "Update Checking")) {
        if (cn) {
            ImGui::TextWrapped("点击菜单栏的 \"Check for Updates\" 按钮，在浏览器中打开 GitHub Releases 页面下载最新版本。");
            ImGui::BulletText("下载最新的 LogCater.zip 并解压覆盖现有安装。");
            ImGui::BulletText("替换 logcater.exe 和 adb/ 文件夹即可完成升级。");
            ImGui::BulletText("设置（缩放比例、窗口位置、Tag 历史）会自动保留。");
        } else {
            ImGui::TextWrapped("Click \"Check for Updates\" in the menu bar to open the GitHub Releases page in your browser.");
            ImGui::BulletText("Download the latest LogCater.zip and extract it over your existing installation.");
            ImGui::BulletText("Replace logcater.exe and the adb/ folder with the new version to upgrade.");
            ImGui::BulletText("Your settings (zoom level, window position, tag history) are preserved automatically.");
        }
    }

    // --- 故障排除 ---
    if (ImGui::CollapsingHeader(cn ? "故障排除" : "Troubleshooting")) {
        if (cn) {
            ImGui::BulletText("设备未被检测到：");
            ImGui::Indent();
            ImGui::TextWrapped("确保 USB 调试已开启且设备已授权。在终端中运行 'adb devices' 验证。");
            ImGui::Unindent();
            ImGui::BulletText("Windows Defender 误报病毒：");
            ImGui::Indent();
            ImGui::TextWrapped("LogCater 完全开源且安全。将 logcater.exe 添加到 Defender 排除项，或从源码编译。");
            ImGui::Unindent();
            ImGui::BulletText("Logcat 无输出或不更新：");
            ImGui::Indent();
            ImGui::TextWrapped("确保没有其他 adb logcat 进程在运行。尝试清除日志缓冲区或重启设备的 adb 守护进程。");
            ImGui::Unindent();
            ImGui::BulletText("文件浏览器显示空目录：");
            ImGui::Indent();
            ImGui::TextWrapped("某些目录需要 root 权限或应用为 debuggable。尝试 /sdcard/ 路径以获取非 root 访问。");
            ImGui::Unindent();
        } else {
            ImGui::BulletText("Device not detected:");
            ImGui::Indent();
            ImGui::TextWrapped("Ensure USB Debugging is enabled and the device is authorized. "
                               "Run 'adb devices' in a terminal to verify.");
            ImGui::Unindent();
            ImGui::BulletText("Windows Defender flags LogCater as suspicious:");
            ImGui::Indent();
            ImGui::TextWrapped("LogCater is open-source and safe. "
                               "Add logcater.exe to your Defender exclusions, or build from source.");
            ImGui::Unindent();
            ImGui::BulletText("Logcat is empty or not updating:");
            ImGui::Indent();
            ImGui::TextWrapped("Make sure no other adb logcat process is running. "
                               "Try clearing the log buffer or restarting the device's adb daemon.");
            ImGui::Unindent();
            ImGui::BulletText("File browser shows empty directory:");
            ImGui::Indent();
            ImGui::TextWrapped("Some directories require root access or the app to be debuggable. "
                               "Try /sdcard/ paths for non-root access.");
            ImGui::Unindent();
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                       "LogCater — MIT License | github.com/kefengwei/LogCater");
}
