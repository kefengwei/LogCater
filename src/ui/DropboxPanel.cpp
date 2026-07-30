#include "DropboxPanel.h"
#include "core/Settings.h"
#include "util/Timestamp.h"
#include "imgui.h"
#include <set>
#include <algorithm>
#include <Windows.h>
#include <commdlg.h>
#include <fstream>

DropboxPanel::DropboxPanel() = default;
DropboxPanel::~DropboxPanel() = default;

std::vector<std::string> DropboxPanel::getUniqueTypes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::set<std::string> types;
    for (const auto& e : m_entries) {
        types.insert(e.tag);
    }
    return std::vector<std::string>(types.begin(), types.end());
}

void DropboxPanel::fetchEntries(const std::string& deviceSerial) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (deviceSerial.empty() || m_loading) return;
        m_loading = true;
    }

    m_reader.listEntries(deviceSerial,
        [this](std::vector<DropboxEntry> entries) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_entries = std::move(entries);
            m_hasEntries = true;
            m_loading = false;
        });
}

void DropboxPanel::render(Settings& settings, const std::string& deviceSerial) {
    // Take a snapshot of shared state under lock
    std::vector<DropboxEntry> entriesSnapshot;
    bool loadingSnap, hasEntriesSnap;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        entriesSnapshot = m_entries;
        loadingSnap = m_loading;
        hasEntriesSnap = m_hasEntries;
    }

    // --- Controls ---
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Type Filter:");
    ImGui::SameLine();

    auto types = getUniqueTypes();
    ImGui::SetNextItemWidth(220);

    if (ImGui::BeginCombo("##dropboxType", m_typeFilter.empty() ? "(all types)" : m_typeFilter.c_str())) {
        if (ImGui::Selectable("(all types)", m_typeFilter.empty())) {
            m_typeFilter.clear();
        }
        for (const auto& t : types) {
            bool selected = (t == m_typeFilter);
            if (ImGui::Selectable(t.c_str(), selected)) {
                m_typeFilter = t;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("Fetch Entries")) {
        fetchEntries(deviceSerial);
    }

    if (loadingSnap) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "Loading...");
    }

    // --- Stats ---
    size_t filteredCount = 0;
    for (const auto& e : entriesSnapshot) {
        if (m_typeFilter.empty() || e.tag == m_typeFilter) {
            filteredCount++;
        }
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                       "Showing %zu entries", filteredCount);

    ImGui::Separator();

    // --- Entry table ---
    if (!hasEntriesSnap) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           "Click 'Fetch Entries' to load dropbox data from device.");
        return;
    }

    ImGui::BeginChild("DropboxTable", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    if (ImGui::BeginTable("##dropboxEntries", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        int rowIdx = 0;
        for (const auto& entry : entriesSnapshot) {
            // Apply type filter
            if (!m_typeFilter.empty() && entry.tag != m_typeFilter) continue;

            // Use sequential row index as unique ID
            ImGui::PushID(rowIdx++);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // Clickable tag
            if (ImGui::Selectable(entry.tag.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                m_selectedTitle = entry.tag + " - " + entry.timeStr;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_loading = true;
                }
                m_reader.getEntry(deviceSerial, entry.tag, entry.timestampMillis,
                    [this](std::string content) {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_selectedContent = std::move(content);
                        m_showDetail = true;
                        m_loading = false;
                    });
            }

            // Double-click to view detail
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                m_selectedTitle = entry.tag + " - " + entry.timeStr;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_loading = true;
                }
                auto tag = entry.tag;
                auto ts = entry.timestampMillis;
                m_reader.getEntry(deviceSerial, tag, ts,
                    [this](std::string content) {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_selectedContent = std::move(content);
                        m_showDetail = true;
                        m_loading = false;
                    });
            }

            ImGui::PopID();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.timeStr.c_str());

            ImGui::TableNextColumn();
            char sizeBuf[32];
            if (entry.sizeBytes >= 1024) {
                snprintf(sizeBuf, sizeof(sizeBuf), "%.1f KB", entry.sizeBytes / 1024.0f);
            } else {
                snprintf(sizeBuf, sizeof(sizeBuf), "%d B", entry.sizeBytes);
            }
            ImGui::TextUnformatted(sizeBuf);
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();

    // --- Detail popup ---
    if (m_showDetail) {
        ImGui::OpenPopup("Dropbox Entry Detail");
        m_showDetail = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Dropbox Entry Detail", nullptr)) {
        ImGui::TextUnformatted(m_selectedTitle.c_str());
        ImGui::Separator();

        ImGui::BeginChild("DetailContent", ImVec2(0, -30), false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(m_selectedContent.c_str());
        ImGui::EndChild();

        if (ImGui::Button("Export", ImVec2(100, 0))) {
            // Build default filename: tag_timestamp.txt
            std::string defaultName = m_selectedTitle;
            // Replace characters unsafe for filenames
            for (auto& c : defaultName) {
                if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                    c == '"' || c == '<' || c == '>' || c == '|') c = '_';
            }
            defaultName += ".txt";

            char filePath[MAX_PATH * 2] = {};
            std::strncpy(filePath, defaultName.c_str(), sizeof(filePath) - 1);

            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = nullptr;
            ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = filePath;
            ofn.nMaxFile = sizeof(filePath);
            ofn.lpstrDefExt = "txt";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

            if (GetSaveFileNameA(&ofn)) {
                std::ofstream out(filePath);
                if (out.is_open()) {
                    out << m_selectedContent;
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
