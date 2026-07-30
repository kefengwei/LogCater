#include "FileBrowserPanel.h"
#include "adb/DeviceManager.h"
#include "core/AdbProcess.h"
#include "core/Settings.h"
#include "imgui.h"
#include <thread>
#include <sstream>
#include <regex>
#include <set>
#include <algorithm>
#include <cstring>
#include <Windows.h>
#include <commdlg.h>

// Global file drop queue from main.cpp
extern std::vector<std::string> g_droppedFiles;
extern std::mutex g_dropMutex;

FileBrowserPanel::FileBrowserPanel() = default;
FileBrowserPanel::~FileBrowserPanel() = default;

bool FileBrowserPanel::runLs(const std::string& deviceSerial, const std::string& path,
                              std::vector<FileEntry>& out, std::string& error) {
    AdbProcess proc;
    std::string output;
    std::mutex outMutex;

    // Direct ls for /sdcard paths (no run-as needed)
    bool ok = proc.start({"-s", deviceSerial, "shell", "ls", "-la", path},
        [&](const std::string& line) {
            std::lock_guard<std::mutex> lock(outMutex);
            output += line + "\n";
        });

    if (!ok) {
        error = "Failed to start adb process";
        return false;
    }

    proc.waitForExit(5000);
    proc.stop();

    if (output.empty()) {
        error = "No output (app may not be debuggable, or path is inaccessible)";
        return false;
    }

    if (output.find("Permission denied") != std::string::npos ||
        output.find("No such file") != std::string::npos ||
        output.find("opendir failed") != std::string::npos) {
        error = output;
        return false;
    }

    out.clear();
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        // Skip "total N" line
        if (line.find("total ") == 0) continue;

        // ls -la format:
        // drwxr-xr-x  2 root  root  4096 2024-01-01 12:00 dirname
        // drwxr-xr-x  2 root  root  4096 Jan 01 12:00  dirname
        // The date/time part is variable-width. Use regex to parse robustly.
        std::regex lsRe(R"(^(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(.+)$)");
        std::smatch match;
        if (!std::regex_search(line, match, lsRe)) continue;

        FileEntry entry;
        entry.permissions = match[1].str();
        entry.isDir = (entry.permissions[0] == 'd');
        entry.size = match[5].str();

        // The last capture group contains date+time+name.
        // Date/time can be: "2026-07-24 11:12 files" (2 tokens) or
        // "Jan 24 11:06 files" (3 tokens) or "Jan 24  2025 files" (3 tokens).
        // Strategy: split the rest, skip tokens matching date/time patterns,
        // the remainder is the filename.
        std::string rest = match[6].str();
        std::istringstream rss(rest);
        std::vector<std::string> tokens;
        std::string tok;
        while (rss >> tok) tokens.push_back(tok);

        // Find where the name starts: skip date/time tokens
        size_t nameStart = 0;
        for (size_t ti = 0; ti < tokens.size(); ti++) {
            bool isDateTime = false;
            // Time pattern: "HH:MM" or "HH:MM:SS"
            if (tokens[ti].find(':') != std::string::npos) isDateTime = true;
            // Date pattern: "YYYY-MM-DD"
            if (tokens[ti].size() == 10 && tokens[ti][4] == '-' && tokens[ti][7] == '-') isDateTime = true;
            // Year pattern: "YYYY" (4 digits)
            if (tokens[ti].size() == 4 && std::all_of(tokens[ti].begin(), tokens[ti].end(), ::isdigit)) isDateTime = true;
            // Month pattern: 3-letter month
            static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                           "Jul","Aug","Sep","Oct","Nov","Dec"};
            for (const char* m : months) {
                if (tokens[ti] == m) { isDateTime = true; break; }
            }
            if (!isDateTime) {
                nameStart = ti;
                break;
            }
        }

        // Build name from remaining tokens (may contain spaces)
        for (size_t ti = nameStart; ti < tokens.size(); ti++) {
            if (!entry.name.empty()) entry.name += " ";
            entry.name += tokens[ti];
        }

        if (entry.name.empty() || entry.name == "." || entry.name == "..") continue;
        out.push_back(std::move(entry));
    }

    // Sort: directories first, then alphabetical
    std::sort(out.begin(), out.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return a.name < b.name;
    });

    return true;
}

void FileBrowserPanel::navigateTo(const std::string& deviceSerial, const std::string& path) {
    m_currentPath = path;
    refreshListing(deviceSerial);
}

void FileBrowserPanel::goUp() {
    if (m_currentPath == "/") return;
    size_t pos = m_currentPath.rfind('/');
    if (pos == std::string::npos) return;
    m_currentPath = (pos == 0) ? "/" : m_currentPath.substr(0, pos);
    // Can't refresh here without device serial; handled in render()
}

void FileBrowserPanel::refreshListing(const std::string& deviceSerial) {
    m_loading = true;
    m_error.clear();
    m_entries.clear();

    std::thread([this, deviceSerial]() {
        std::vector<FileEntry> entries;
        std::string error;
        if (runLs(deviceSerial, m_currentPath, entries, error)) {
            m_entries = std::move(entries);
        } else {
            m_error = error;
        }
        m_loading = false;
    }).detach();
}

void FileBrowserPanel::viewFile(const std::string& deviceSerial,
                                 const std::string& path, const std::string& name) {
    m_contentTitle = name;
    m_contentText.clear();
    m_contentLines.clear();
    m_contentLoading = true;
    m_showContent = true;

    std::string fullPath = path;
    if (fullPath.back() != '/') fullPath += '/';
    fullPath += name;

    std::thread([this, deviceSerial, fullPath]() {
        AdbProcess proc;
        std::string output;
        proc.start({"-s", deviceSerial, "shell", "cat", fullPath},
            [&](const std::string& line) {
                output += line + "\n";
            });
        proc.waitForExit(10000);
        proc.stop();

        // Parse all lines once — cache (text, color) pairs
        std::vector<std::pair<std::string, ImVec4>> parsed;
        std::istringstream lines(output);
        std::string line;
        std::regex logcatRe(R"(\s([VDIWEF])\s)");
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            ImVec4 color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            std::smatch m;
            if (std::regex_search(line, m, logcatRe)) {
                switch (m[1].str()[0]) {
                    case 'V': color = ImVec4(0.50f,0.50f,0.50f,1.0f); break;
                    case 'D': color = ImVec4(0.75f,0.75f,0.75f,1.0f); break;
                    case 'I': color = ImVec4(0.35f,0.90f,0.35f,1.0f); break;
                    case 'W': color = ImVec4(1.00f,0.80f,0.00f,1.0f); break;
                    case 'E': color = ImVec4(1.00f,0.30f,0.30f,1.0f); break;
                    case 'F': color = ImVec4(1.00f,0.05f,1.00f,1.0f); break;
                }
            } else if (line.find("Error:") != std::string::npos || line.find("error:") != std::string::npos)
                color = ImVec4(1.00f,0.30f,0.30f,1.0f);
            else if (line.find("Warning:") != std::string::npos || line.find("warning:") != std::string::npos)
                color = ImVec4(1.00f,0.80f,0.00f,1.0f);
            else if (line.find("Fatal:") != std::string::npos || line.find("fatal:") != std::string::npos)
                color = ImVec4(1.00f,0.05f,1.00f,1.0f);
            else if (line.find("Display:") != std::string::npos)
                color = ImVec4(0.35f,0.90f,0.35f,1.0f);
            else if (line.find("Verbose:") != std::string::npos || line.find("VeryVerbose:") != std::string::npos)
                color = ImVec4(0.50f,0.50f,0.55f,1.0f);
            else if (line.find("Log") == 0 || (!line.empty() && line[0] == '['))
                color = ImVec4(0.65f,0.65f,0.70f,1.0f);
            parsed.emplace_back(std::move(line), color);
        }

        m_contentLines = std::move(parsed);
        m_contentLoading = false;
    }).detach();
}

void FileBrowserPanel::pullFile(const std::string& deviceSerial,
                                 const std::string& path, const std::string& name) {
    std::string fullPath = path;
    if (fullPath.back() != '/') fullPath += '/';
    fullPath += name;

    char saveName[MAX_PATH];
    std::strncpy(saveName, name.c_str(), sizeof(saveName) - 1);
    saveName[sizeof(saveName) - 1] = '\0';

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "All Files (*.*)\0*.*\0";
    ofn.lpstrFile = saveName;
    ofn.nMaxFile = sizeof(saveName);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (GetSaveFileNameA(&ofn)) {
        std::thread([deviceSerial, fullPath, savePath = std::string(saveName)]() {
            AdbProcess proc;
            proc.start({"-s", deviceSerial, "pull", fullPath, savePath},
                [](const std::string&) {});
            proc.waitForExit(30000);
            proc.stop();
        }).detach();
    }
}

void FileBrowserPanel::uploadFile(const std::string& deviceSerial, const std::string& localPath) {
    // Extract filename from local path
    std::string name = localPath;
    size_t lastSlash = name.find_last_of("/\\");
    if (lastSlash != std::string::npos) name = name.substr(lastSlash + 1);

    std::string remotePath = m_currentPath;
    if (remotePath.back() != '/') remotePath += '/';
    remotePath += name;

    std::thread([deviceSerial, localPath, remotePath, this]() {
        AdbProcess proc;
        proc.start({"-s", deviceSerial, "push", localPath, remotePath},
            [](const std::string&) {});
        proc.waitForExit(30000);
        proc.stop();
        // Refresh listing after upload
        m_loading = true;
        m_error.clear();
        std::vector<FileEntry> entries;
        std::string error;
        if (runLs(deviceSerial, m_currentPath, entries, error)) {
            m_entries = std::move(entries);
        }
        m_loading = false;
    }).detach();
}

void FileBrowserPanel::render(const std::string& deviceSerial, DeviceManager& dm, Settings& settings) {
    // --- Process dropped files ---
    {
        std::lock_guard<std::mutex> lock(g_dropMutex);
        if (!g_droppedFiles.empty() && !m_currentPath.empty()) {
            for (const auto& f : g_droppedFiles) {
                uploadFile(deviceSerial, f);
            }
            g_droppedFiles.clear();
        }
    }

    // --- Package selector (combo with filter) ---
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Package:");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(280);
    std::string preview = m_selectedPackage.empty() ? "(select package...)" : m_selectedPackage;
    if (ImGui::BeginCombo("##pkgSelect", preview.c_str())) {
        // Filter input at top of combo
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##pkgFilterInput", "filter...",
                                  m_packageFilterBuf, sizeof(m_packageFilterBuf));
        std::string filter = m_packageFilterBuf;

        // Merge running process names + installed package names (no duplicates)
        std::set<std::string> seen;
        auto procNames = dm.allProcessNames();
        auto instPkgs = dm.installedPackages();

        // Process names first (running apps — most relevant for file browsing)
        for (const auto& name : procNames) {
            if (!filter.empty() && name.find(filter) == std::string::npos) continue;
            if (!seen.insert(name).second) continue;
            bool isSel = (name == m_selectedPackage);
            if (ImGui::Selectable(name.c_str(), isSel)) {
                m_selectedPackage = name;
                std::strncpy(m_packageFilterBuf, name.c_str(), sizeof(m_packageFilterBuf) - 1);
                m_packageFilterBuf[sizeof(m_packageFilterBuf) - 1] = '\0';
                std::string pkgPath = "/sdcard/Android/data/" + name;
                navigateTo(deviceSerial, pkgPath);
            }
            if (isSel) ImGui::SetItemDefaultFocus();
        }

        // Then installed packages not already shown
        if (!instPkgs.empty() && !procNames.empty()) {
            ImGui::Separator();
        }
        for (const auto& name : instPkgs) {
            if (!filter.empty() && name.find(filter) == std::string::npos) continue;
            if (!seen.insert(name).second) continue;
            bool isSel = (name == m_selectedPackage);
            if (ImGui::Selectable(name.c_str(), isSel)) {
                m_selectedPackage = name;
                std::strncpy(m_packageFilterBuf, name.c_str(), sizeof(m_packageFilterBuf) - 1);
                m_packageFilterBuf[sizeof(m_packageFilterBuf) - 1] = '\0';
                std::string pkgPath = "/sdcard/Android/data/" + name;
                navigateTo(deviceSerial, pkgPath);
            }
            if (isSel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("Go") && !m_selectedPackage.empty()) {
        std::string pkgPath = "/sdcard/Android/data/" + m_selectedPackage;
        navigateTo(deviceSerial, pkgPath);
    }

    ImGui::Separator();

    // --- Path bar ---
    ImGui::TextUnformatted("Path:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%s", m_currentPath.c_str());

    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) {
        refreshListing(deviceSerial);
    }

    // --- Quick nav buttons ---
    ImGui::SameLine();
    if (ImGui::SmallButton("/sdcard")) {
        navigateTo(deviceSerial, "/sdcard");
    }
    if (!m_selectedPackage.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("pkg data")) {
            navigateTo(deviceSerial, "/sdcard/Android/data/" + m_selectedPackage);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("files")) {
            navigateTo(deviceSerial, "/sdcard/Android/data/" + m_selectedPackage + "/files");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("cache")) {
            navigateTo(deviceSerial, "/sdcard/Android/data/" + m_selectedPackage + "/cache");
        }
    }

    // --- Bookmarks ---
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Bookmark")) {
        if (!m_currentPath.empty() &&
            std::find(settings.fileBrowserBookmarks.begin(),
                      settings.fileBrowserBookmarks.end(),
                      m_currentPath) == settings.fileBrowserBookmarks.end()) {
            settings.fileBrowserBookmarks.push_back(m_currentPath);
            if (settings.fileBrowserBookmarks.size() > Settings::MAX_BOOKMARKS) {
                settings.fileBrowserBookmarks.erase(settings.fileBrowserBookmarks.begin());
            }
        }
    }

    if (!settings.fileBrowserBookmarks.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        for (size_t bi = 0; bi < settings.fileBrowserBookmarks.size(); bi++) {
            ImGui::SameLine();
            ImGui::PushID(static_cast<int>(bi + 1000));
            std::string label = settings.fileBrowserBookmarks[bi];
            size_t lastSlash = label.find_last_of('/');
            if (lastSlash != std::string::npos && lastSlash + 1 < label.size())
                label = label.substr(lastSlash + 1);
            if (ImGui::SmallButton(label.c_str())) {
                navigateTo(deviceSerial, settings.fileBrowserBookmarks[bi]);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", settings.fileBrowserBookmarks[bi].c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                settings.fileBrowserBookmarks.erase(settings.fileBrowserBookmarks.begin() + bi);
            ImGui::PopID();
        }
    }

    ImGui::Separator();

    // --- File listing ---
    if (m_currentPath.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           "Select a package to browse its files.");
        return;
    }

    if (m_loading) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "Loading...");
        return;
    }

    if (!m_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", m_error.c_str());
        ImGui::PopStyleColor();
        return;
    }

    ImGui::BeginChild("FileList", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    if (ImGui::BeginTable("##fileTable", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Permissions", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableHeadersRow();

        // ".." row at top for go-up navigation
        if (m_currentPath != "/") {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImVec2 rowStart = ImGui::GetCursorScreenPos();
            float rowW = ImGui::GetContentRegionAvail().x;
            ImGui::InvisibleButton("##upRow", ImVec2(rowW, ImGui::GetTextLineHeightWithSpacing()));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                goUp();
                refreshListing(deviceSerial);
            }
            ImGui::SetCursorScreenPos(rowStart);
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "[DIR]  ..");
            ImGui::TableNextColumn();
            ImGui::TableNextColumn();
            ImGui::TableNextColumn();
        }

        for (size_t i = 0; i < m_entries.size(); i++) {
            const auto& entry = m_entries[i];
            ImGui::PushID(static_cast<int>(i));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // For directories, use full-row InvisibleButton for double-click
            if (entry.isDir) {
                ImVec2 rowStart = ImGui::GetCursorScreenPos();
                float rowW = ImGui::GetContentRegionAvail().x;

                ImGui::InvisibleButton("##dirRow", ImVec2(rowW, ImGui::GetTextLineHeightWithSpacing()));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    std::string newPath = m_currentPath;
                    if (newPath.back() != '/') newPath += '/';
                    newPath += entry.name;
                    navigateTo(deviceSerial, newPath);
                }

                ImGui::SetCursorScreenPos(rowStart);
                ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "[DIR]  %s", entry.name.c_str());
            } else {
                ImVec2 rowStart = ImGui::GetCursorScreenPos();
                float rowW = ImGui::GetContentRegionAvail().x;

                ImGui::InvisibleButton("##fileRow", ImVec2(rowW, ImGui::GetTextLineHeightWithSpacing()));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    viewFile(deviceSerial, m_currentPath, entry.name);
                }

                ImGui::SetCursorScreenPos(rowStart);
                ImGui::TextUnformatted(entry.name.c_str());
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.size.c_str());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.permissions.c_str());

            ImGui::TableNextColumn();
            if (!entry.isDir) {
                if (ImGui::SmallButton("View")) {
                    viewFile(deviceSerial, m_currentPath, entry.name);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Down")) {
                    pullFile(deviceSerial, m_currentPath, entry.name);
                }
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();

    // --- File content viewer popup ---
    if (m_showContent) {
        ImGui::OpenPopup("File Content");
        m_showContent = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("File Content", nullptr)) {
        ImGui::TextUnformatted(m_contentTitle.c_str());
        ImGui::Separator();

        if (m_contentLoading) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "Loading...");
        } else if (!m_contentLines.empty()) {
            // Virtual-scrolled colorized log view
            int totalLines = static_cast<int>(m_contentLines.size());
            ImGui::BeginChild("##fileContent", ImVec2(0, -30), false,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGuiListClipper clipper;
            clipper.Begin(totalLines, ImGui::GetTextLineHeightWithSpacing());
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    ImGui::TextColored(m_contentLines[i].second, "%s",
                                       m_contentLines[i].first.c_str());
                }
            }
            clipper.End();
            ImGui::EndChild();
        } else {
            ImGui::BeginChild("##fileContent", ImVec2(0, -30), false,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(m_contentText.c_str());
            ImGui::EndChild();
        }

        if (ImGui::Button("Close", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
