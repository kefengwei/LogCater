#include "FilterBar.h"
#include "core/Settings.h"
#include "imgui.h"
#include <cstring>

static const char* kLevelNames[] = {"Verbose", "Debug", "Info", "Warning", "Error", "Fatal"};
static const char  kLevelChars[] = {'V', 'D', 'I', 'W', 'E', 'F'};

void FilterBar::render(Settings& settings) {
    // --- Text filter ---
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Filter:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    if (m_textBuf[0] == '\0' && !m_textFilter.empty()) {
        std::strncpy(m_textBuf, m_textFilter.c_str(), sizeof(m_textBuf) - 1);
    }
    if (ImGui::InputTextWithHint("##textFilter", "keyword...", m_textBuf, sizeof(m_textBuf))) {
        m_textFilter = m_textBuf;
    }
    ImGui::SameLine();

    // --- Tag filter + clear button ---
    ImGui::TextUnformatted("Tag:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    if (m_tagBuf[0] == '\0' && !m_tagFilter.empty()) {
        std::strncpy(m_tagBuf, m_tagFilter.c_str(), sizeof(m_tagBuf) - 1);
    }
    if (ImGui::InputTextWithHint("##tagFilter", "tag1;tag2...", m_tagBuf, sizeof(m_tagBuf))) {
        m_tagFilter = m_tagBuf;
        // Add each individual tag to history (split by ';')
        if (!m_tagFilter.empty()) {
            auto addSingle = [&](const std::string& segment) {
                std::string trimmed;
                for (char c : segment) {
                    if (c != ' ' && c != '\t') trimmed += c;
                }
                if (!trimmed.empty()) settings.addTagToHistory(trimmed);
            };
            std::string filter = m_tagFilter;
            size_t pos = 0;
            while (pos <= filter.size()) {
                size_t end = filter.find(';', pos);
                if (end == std::string::npos) end = filter.size();
                addSingle(filter.substr(pos, end - pos));
                pos = end + 1;
            }
        }
    }

    // Clear tag button (X)
    if (!m_tagFilter.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(" X ##clearTag")) {
            m_tagFilter.clear();
            m_tagBuf[0] = '\0';
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear tag filter");
    }

    // History button
    ImGui::SameLine();
    if (ImGui::ArrowButton("##tagHistory", ImGuiDir_Down)) {
        ImGui::OpenPopup("TagHistoryPopup");
    }
    if (ImGui::BeginPopup("TagHistoryPopup")) {
        ImGui::TextUnformatted("Tag History");
        ImGui::Separator();
        if (ImGui::Selectable("(clear filter)")) {
            m_tagFilter.clear();
            m_tagBuf[0] = '\0';
        }
        for (const auto& tag : settings.tagHistory) {
            if (ImGui::Selectable(tag.c_str())) {
                m_tagFilter = tag;
                std::strncpy(m_tagBuf, tag.c_str(), sizeof(m_tagBuf) - 1);
                m_tagBuf[sizeof(m_tagBuf) - 1] = '\0';
            }
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // --- Level multi-select ---
    ImGui::TextUnformatted("Level:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);

    // Build preview string
    std::string preview;
    int selCount = 0;
    for (int i = 0; i < 6; i++) {
        if (m_levelMask & (1 << i)) selCount++;
    }
    if (selCount == 6) {
        preview = "All";
    } else if (selCount == 0) {
        preview = "None";
    } else {
        for (int i = 0; i < 6; i++) {
            if (m_levelMask & (1 << i)) {
                if (!preview.empty()) preview += ",";
                preview += kLevelChars[i];
            }
        }
    }

    if (ImGui::BeginCombo("##levelFilter", preview.c_str())) {
        // Select All / Deselect All
        if (ImGui::Selectable("Select All")) {
            m_levelMask = 0x3F;
        }
        if (ImGui::Selectable("Deselect All")) {
            m_levelMask = 0;
        }

        ImGui::Separator();

        for (int i = 0; i < 6; i++) {
            bool enabled = (m_levelMask & (1 << i)) != 0;
            ImVec4 lvlColor;
            switch (kLevelChars[i]) {
                case 'V': lvlColor = ImVec4(0.50f, 0.50f, 0.50f, 1.0f); break;
                case 'D': lvlColor = ImVec4(0.75f, 0.75f, 0.75f, 1.0f); break;
                case 'I': lvlColor = ImVec4(0.35f, 0.90f, 0.35f, 1.0f); break;
                case 'W': lvlColor = ImVec4(1.00f, 0.80f, 0.00f, 1.0f); break;
                case 'E': lvlColor = ImVec4(1.00f, 0.30f, 0.30f, 1.0f); break;
                case 'F': lvlColor = ImVec4(1.00f, 0.05f, 1.00f, 1.0f); break;
                default:  lvlColor = ImVec4(0.70f, 0.70f, 0.70f, 1.0f); break;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, lvlColor);
            if (ImGui::Checkbox(kLevelNames[i], &enabled)) {
                if (enabled) m_levelMask |= (1 << i);
                else         m_levelMask &= ~(1 << i);
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndCombo();
    }
}
