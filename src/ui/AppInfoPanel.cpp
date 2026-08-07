#include "AppInfoPanel.h"
#include "core/AdbProcess.h"
#include "imgui.h"
#include <thread>
#include <regex>
#include <sstream>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <algorithm>
#include <set>
#include <Windows.h>
#include <commdlg.h>

// Global file drop queue from main.cpp
extern std::vector<std::string> g_droppedFiles;
extern std::mutex g_dropMutex;

namespace {

std::string pickApkFile() {
    char buf[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "APK Files (*.apk)\0*.apk\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Select APK to install";
    if (GetOpenFileNameA(&ofn)) {
        return std::string(buf);
    }
    return "";
}

bool endsWithLower(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != suffix[i])
            return false;
    }
    return true;
}

std::string fileNameOf(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

} // namespace

AppInfoPanel::AppInfoPanel() = default;
AppInfoPanel::~AppInfoPanel() = default;

void AppInfoPanel::showToast(const std::string& msg, const ImVec4& color) {
    m_toastMsg = msg;
    m_toastColor = color;
    m_toastEndTime = static_cast<float>(ImGui::GetTime()) + 4.0f;
}

void AppInfoPanel::renderToast() {
    if (m_toastMsg.empty() || ImGui::GetTime() >= m_toastEndTime) return;

    const ImVec2 textSize = ImGui::CalcTextSize(m_toastMsg.c_str());
    const ImVec2 pad(24.0f, 12.0f);
    const ImVec2 size(textSize.x + pad.x * 2.0f, textSize.y + pad.y * 2.0f);
    const ImVec2 pos((ImGui::GetWindowWidth() - size.x) * 0.5f, 48.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                      IM_COL32(30, 30, 34, 235), 6.0f);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                IM_COL32(120, 120, 130, 220), 6.0f, 0, 1.0f);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), ImGui::ColorConvertFloat4ToU32(m_toastColor),
                m_toastMsg.c_str());
}

void AppInfoPanel::installApks(const std::string& deviceSerial, std::vector<std::string> paths) {
    if (paths.empty() || deviceSerial.empty() || m_installing.load()) return;
    m_installing.store(true);
    m_pendingRefresh.store(false);
    {
        std::lock_guard<std::mutex> lock(m_installMutex);
        m_installStatus = InstallStatus{};
        m_installStatus.total = static_cast<int>(paths.size());
    }
    m_lastNotifiedDone = 0;
    showToast(paths.size() == 1
                  ? "开始安装: " + fileNameOf(paths[0])
                  : "开始安装 " + std::to_string(paths.size()) + " 个 APK...",
              ImVec4(0.5f, 0.8f, 1.0f, 1.0f));

    std::thread([this, deviceSerial, paths = std::move(paths)]() {
        int done = 0;
        for (const auto& path : paths) {
            {
                std::lock_guard<std::mutex> lock(m_installMutex);
                m_installStatus.currentFile = path;
                m_installStatus.message.clear();
            }

            std::string out;
            AdbProcess proc;
            proc.start({"-s", deviceSerial, "install", "-r", path},
                [&](const std::string& line) { out += line + "\n"; });
            proc.waitForExit(120000);
            proc.stop();

            bool ok = out.find("Success") != std::string::npos;
            {
                std::lock_guard<std::mutex> lock(m_installMutex);
                m_installStatus.done = ++done;
                m_installStatus.lastFailed = !ok;
                m_installStatus.message = ok ? "Success" : (out.empty() ? "No output (install timed out?)" : out);
            }
        }
        m_installing.store(false);
        m_pendingRefresh.store(true);
    }).detach();
}

void AppInfoPanel::refreshAppList(const std::string& deviceSerial) {
    if (deviceSerial.empty() || m_loading.load()) return;

    m_loading.store(true);
    m_loadedCount.store(0);
    m_totalCount.store(0);

    // Capture source filter value for the background thread
    int appSource = m_appSource;

    std::thread([this, deviceSerial, appSource]() {
        // --- Step 1: Get filtered package list (fast, ~1s) ---
        std::set<std::string> targetPkgs;
        {
            const char* listFlag = "-3";       // third-party
            if (appSource == 1) listFlag = "-s"; // system
            else if (appSource == 2) listFlag = ""; // all

            AdbProcess proc;
            std::string out;
            proc.start({"-s", deviceSerial, "shell", std::string("pm list packages ") + listFlag},
                [&](const std::string& line) {
                    // Parse "package:com.example.app"
                    if (line.rfind("package:", 0) == 0) {
                        targetPkgs.insert(line.substr(8));
                    }
                });
            proc.waitForExit(5000);
            proc.stop();

            if (targetPkgs.empty() && appSource != 2) {
                m_loading.store(false);
                return; // no packages of this type
            }
        }

        m_totalCount.store(static_cast<int>(targetPkgs.size()));

        // --- Step 2: Get all package details in ONE dumpsys call (~5-10s) ---
        // Captures Package[...], versionCode=, versionName=, and label= lines.
        // ' label=' (space-prefixed) avoids matching labelRes=/nonLocalizedLabel=.
        std::vector<AppInfo> newApps;
        AppInfo current;
        std::string accumulated;
        {
            AdbProcess proc;
            proc.start({"-s", deviceSerial, "shell",
                        "dumpsys package | grep -e 'Package \\[' -e 'versionCode=' -e 'versionName=' -e ' label='"},
                [&](const std::string& line) {
                    accumulated += line + "\n";
                });
            proc.waitForExit(30000);
            proc.stop();
        }

        // Parse the dumpsys output
        {
            std::istringstream iss(accumulated);
            std::string line;
            std::regex pkgRe(R"(Package\s*\[([^\]]+)\])");
            std::regex vcRe(R"(versionCode=(\d+))");
            std::regex vnRe(R"(versionName=(\S+))");
            std::regex tsRe(R"(targetSdk=(\d+))");
            std::regex labelRe(R"(\s+label=(\S.*))");   // ' label=App Name'
            std::smatch m;

            while (std::getline(iss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;

                if (std::regex_search(line, m, pkgRe)) {
                    // Save previous entry
                    if (!current.packageName.empty() && !targetPkgs.empty()) {
                        if (targetPkgs.count(current.packageName)) {
                            current.detailsLoaded = true;
                            newApps.push_back(std::move(current));
                        }
                    } else if (!current.packageName.empty() && targetPkgs.empty()) {
                        current.detailsLoaded = true;
                        newApps.push_back(std::move(current));
                    }
                    current = AppInfo{};
                    current.packageName = m[1].str();
                } else if (std::regex_search(line, m, vcRe)) {
                    current.versionCode = std::stoi(m[1].str());
                    if (std::regex_search(line, m, tsRe)) {
                        current.targetSdk = std::stoi(m[1].str());
                    }
                } else if (std::regex_search(line, m, vnRe)) {
                    current.versionName = m[1].str();
                } else if (std::regex_search(line, m, labelRe)) {
                    std::string lbl = m[1].str();
                    // Trim trailing whitespace
                    while (!lbl.empty() && std::isspace(static_cast<unsigned char>(lbl.back())))
                        lbl.pop_back();
                    if (current.appName.empty() && !lbl.empty()) {
                        current.appName = lbl;
                    }
                }
            }
            // Last entry
            if (!current.packageName.empty()) {
                if (targetPkgs.empty() || targetPkgs.count(current.packageName)) {
                    current.detailsLoaded = true;
                    newApps.push_back(std::move(current));
                }
            }
        }

        if (!newApps.empty()) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_apps = std::move(newApps);
        }
        m_loadedCount.store(static_cast<int>(newApps.size()));
        m_loading.store(false);
    }).detach();
}

void AppInfoPanel::render(const std::string& deviceSerial) {
    // Auto-refresh when device changes or on first open
    if (deviceSerial != m_lastDeviceSerial && !deviceSerial.empty()) {
        m_lastDeviceSerial = deviceSerial;
        m_apps.clear();
    }

    // --- Consume dropped .apk files (drag-and-drop install) ---
    {
        std::vector<std::string> pending;
        {
            std::lock_guard<std::mutex> lock(g_dropMutex);
            for (auto it = g_droppedFiles.begin(); it != g_droppedFiles.end();) {
                if (endsWithLower(*it, ".apk")) {
                    pending.push_back(*it);
                    it = g_droppedFiles.erase(it);
                } else {
                    ++it;
                }
            }
        }
        if (!pending.empty()) {
            installApks(deviceSerial, std::move(pending));
        }
    }

    // Refresh list once after an install batch finishes
    if (m_pendingRefresh.exchange(false) && !deviceSerial.empty() && !m_loading.load()) {
        refreshAppList(deviceSerial);
    }

    // --- Top controls ---
    ImGui::AlignTextToFramePadding();

    // App source radio buttons
    ImGui::TextUnformatted("Source:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Third-party", m_appSource == 0)) {
        if (m_appSource != 0) { m_appSource = 0; m_apps.clear(); }
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("System", m_appSource == 1)) {
        if (m_appSource != 1) { m_appSource = 1; m_apps.clear(); }
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("All", m_appSource == 2)) {
        if (m_appSource != 2) { m_appSource = 2; m_apps.clear(); }
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 280);

    if (ImGui::Button("Refresh")) {
        m_apps.clear();
        refreshAppList(deviceSerial);
    }

    ImGui::SameLine();
    if (ImGui::Button("Install APK")) {
        std::string apk = pickApkFile();
        if (!apk.empty()) {
            installApks(deviceSerial, {apk});
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Select an APK file to install to the device");
    }

    // End-of-batch toast (fires once per batch)
    {
        std::lock_guard<std::mutex> lock(m_installMutex);
        if (!m_installing.load() && m_installStatus.done > 0 &&
            m_installStatus.done >= m_installStatus.total &&
            m_lastNotifiedDone != m_installStatus.done) {
            m_lastNotifiedDone = m_installStatus.done;
            if (m_installStatus.lastFailed) {
                std::string msg = "安装失败: " + m_installStatus.message;
                if (msg.size() > 120) msg = msg.substr(0, 120) + "...";
                showToast(msg, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            } else {
                showToast("安装完成 (共 " + std::to_string(m_installStatus.done) + " 个)",
                          ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
            }
        }
    }

    // --- Install status (full line, wrapped so it is never truncated) ---
    {
        std::lock_guard<std::mutex> lock(m_installMutex);
        if (m_installing.load()) {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
                               "Installing: %s (%d/%d)...",
                               m_installStatus.currentFile.c_str(),
                               m_installStatus.done, m_installStatus.total);
            float frac = m_installStatus.total > 0
                             ? static_cast<float>(m_installStatus.done) / m_installStatus.total
                             : 0.0f;
            char pct[32];
            std::snprintf(pct, sizeof(pct), "%.0f%%", frac * 100.0f);
            ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0.0f), pct);
        } else if (m_installStatus.done > 0 && !m_installStatus.message.empty()) {
            if (m_installStatus.lastFailed) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Install failed:");
                ImGui::SameLine();
                ImGui::TextWrapped("%s", m_installStatus.message.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Install OK");
            }
        }
    }

    // Auto-load: if list is empty, trigger refresh
    bool needRefresh = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        needRefresh = m_apps.empty() && !m_loading.load() && !deviceSerial.empty();
    }
    if (needRefresh) {
        refreshAppList(deviceSerial);
    }

    // Loading indicator
    if (m_loading.load()) {
        ImGui::SameLine();
        if (m_totalCount.load() > 0) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f),
                               "Loading... (%d/%d)", m_loadedCount.load(), m_totalCount.load());
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "Loading...");
        }
    }

    // Filter
    ImGui::TextUnformatted("Filter:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(320);
    char filterBuf[256] = {};
    if (!m_textFilter.empty()) {
        std::strncpy(filterBuf, m_textFilter.c_str(), sizeof(filterBuf) - 1);
    }
    if (ImGui::InputTextWithHint("##appFilter", "search packages...",
                                 filterBuf, sizeof(filterBuf))) {
        m_textFilter = filterBuf;
    }

    // Stats
    ImGui::SameLine();
    int totalVisible = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& app : m_apps) {
            if (m_textFilter.empty() ||
                app.packageName.find(m_textFilter) != std::string::npos ||
                app.appName.find(m_textFilter) != std::string::npos) {
                totalVisible++;
            }
        }
    }
    if (!m_textFilter.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                           "(%d matching)", totalVisible);
    }

    ImGui::Separator();

    // --- App table ---
    // Snapshot under lock
    std::vector<AppInfo> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        snapshot = m_apps;
    }

    if (snapshot.empty() && !m_loading.load()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           snapshot.empty() && m_lastDeviceSerial.empty()
                           ? "Select a device to view installed apps."
                           : "Click 'Refresh' to load installed apps.");
        return;
    }

    ImGui::BeginChild("AppTable", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    if (ImGui::BeginTable("##appTable", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                          ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Package", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Ver.Code", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Target SDK", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        int rowIdx = 0;
        for (const auto& app : snapshot) {
            // Text filter (matches package name or app name)
            if (!m_textFilter.empty() &&
                app.packageName.find(m_textFilter) == std::string::npos &&
                app.appName.find(m_textFilter) == std::string::npos) {
                continue;
            }

            ImGui::PushID(rowIdx++);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // App name (human-readable label)
            if (app.detailsLoaded && !app.appName.empty()) {
                ImGui::TextUnformatted(app.appName.c_str());
            } else if (app.detailsLoaded) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(no label)");
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "...");
            }

            ImGui::TableNextColumn();
            // Selectable full-row: click to copy package name
            if (ImGui::Selectable(app.packageName.c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                ImGui::SetClipboardText(app.packageName.c_str());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to copy package name");
            }

            ImGui::TableNextColumn();
            if (app.detailsLoaded) {
                ImGui::TextUnformatted(app.versionName.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "...");
            }

            ImGui::TableNextColumn();
            if (app.detailsLoaded) {
                ImGui::Text("%d", app.versionCode);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "...");
            }

            ImGui::TableNextColumn();
            if (app.detailsLoaded) {
                ImGui::Text("%d", app.targetSdk);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "...");
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();

    renderToast();
}
