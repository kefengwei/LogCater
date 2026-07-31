#include "AppInfoPanel.h"
#include "core/AdbProcess.h"
#include "imgui.h"
#include <thread>
#include <regex>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <set>

AppInfoPanel::AppInfoPanel() = default;
AppInfoPanel::~AppInfoPanel() = default;

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
    ImGui::SameLine();
    ImGui::TextUnformatted("Filter:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
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
}
