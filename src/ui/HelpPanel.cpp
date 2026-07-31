#include "HelpPanel.h"
#include "imgui.h"

void HelpPanel::render() {
    ImGui::TextWrapped(
        "LogCater is a desktop tool for managing Android devices via ADB. "
        "Connect your device via USB (with USB Debugging enabled), and LogCater "
        "will automatically detect it."
    );

    // --- Getting Started ---
    if (ImGui::CollapsingHeader("Getting Started", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BulletText("Enable USB Debugging on your Android device (Developer Options).");
        ImGui::BulletText("Connect the device to your PC via USB cable.");
        ImGui::BulletText("Select your device from the dropdown in the menu bar.");
        ImGui::BulletText("LogCater bundles adb internally — no Android SDK installation needed.");
        ImGui::BulletText("If your device isn't detected, check that USB Debugging is authorized.");
    }

    // --- Logcat Tab ---
    if (ImGui::CollapsingHeader("Logcat Tab")) {
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

    // --- Dropbox Tab ---
    if (ImGui::CollapsingHeader("Dropbox Tab")) {
        ImGui::TextWrapped("View Android system dropbox entries (crashes, ANRs, WTF reports) via 'adb shell dumpsys dropbox'.");
        ImGui::BulletText("Click \"Fetch Entries\" to load the list of available dropbox entries.");
        ImGui::BulletText("Filter by type using the dropdown (e.g. data_app_crash, system_server_wtf).");
        ImGui::BulletText("Double-click an entry to view its full content in a popup window.");
        ImGui::BulletText("Export — save entry content to a text file on your PC.");
    }

    // --- Files Tab ---
    if (ImGui::CollapsingHeader("Files Tab")) {
        ImGui::TextWrapped("Browse the Android device file system, view and transfer files.");
        ImGui::BulletText("Package selector — choose an app package to browse its data directory.");
        ImGui::BulletText("Quick-nav buttons for common locations: /data/data/, /sdcard/, etc.");
        ImGui::BulletText("Double-click a .log or .txt file to preview its contents with syntax highlighting.");
        ImGui::BulletText("UE4/Unreal Engine log syntax highlighting is supported for .log files.");
        ImGui::BulletText("Download — pull a file from the device to your PC.");
        ImGui::BulletText("Upload — drag & drop a file from Windows Explorer onto the table to push it to the device.");
        ImGui::BulletText("Bookmarks — star frequently-used directories for quick access. Saved across sessions.");
    }

    // --- Apps Tab ---
    if (ImGui::CollapsingHeader("Apps Tab")) {
        ImGui::TextWrapped("View detailed information about installed applications on the device.");
        ImGui::BulletText("Source filter — switch between Third-party, System, and All apps.");
        ImGui::BulletText("Shows: Package Name, Version Name, Version Code, Target SDK.");
        ImGui::BulletText("Click a row to copy the package name to the clipboard.");
        ImGui::BulletText("Refresh — reload the app list from the device.");
        ImGui::BulletText("Text filter — search by package name.");
    }

    // --- Shortcuts ---
    if (ImGui::CollapsingHeader("Keyboard Shortcuts")) {
        if (ImGui::BeginTable("ShortcutsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Shortcut");
            ImGui::TableSetupColumn("Action");
            ImGui::TableHeadersRow();

            auto row = [](const char* key, const char* action) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(key);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(action);
            };

            row("Ctrl + MouseWheel Up", "Zoom in (scale UI)");
            row("Ctrl + MouseWheel Down", "Zoom out (scale UI)");
            row("Ctrl + =", "Zoom in");
            row("Ctrl + -", "Zoom out");
            row("Ctrl + 0", "Reset zoom to 100%");

            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextWrapped("UI scale is saved automatically and restored on next launch.");
    }

    // --- Update Checking ---
    if (ImGui::CollapsingHeader("Update Checking")) {
        ImGui::TextWrapped(
            "Click \"Check for Updates\" in the menu bar to open the GitHub Releases page "
            "in your browser, where you can download the latest version."
        );
        ImGui::BulletText("Download the latest LogCater.zip and extract it over your existing installation.");
        ImGui::BulletText("Replace logcater.exe and the adb/ folder with the new version to upgrade.");
        ImGui::BulletText("Your settings (zoom level, window position, tag history) are preserved automatically.");
    }

    // --- Troubleshooting ---
    if (ImGui::CollapsingHeader("Troubleshooting")) {
        ImGui::BulletText("Device not detected:");
        ImGui::Indent();
        ImGui::TextWrapped("Ensure USB Debugging is enabled and the device is authorized. "
                           "Run 'adb devices' in a terminal to verify.");
        ImGui::Unindent();
        ImGui::BulletText("Windows Defender flags LogCater as suspicious:");
        ImGui::Indent();
        ImGui::TextWrapped("This is a false positive — LogCater is open-source and safe. "
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

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                       "LogCater — MIT License | github.com/kefengwei/LogCater");
}
