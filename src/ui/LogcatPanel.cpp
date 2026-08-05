#include "LogcatPanel.h"
#include "adb/DeviceManager.h"
#include "core/Settings.h"
#include "imgui.h"
#include <cstdio>
#include <cstring>

LogcatPanel::LogcatPanel() { m_displayEntries.reserve(MAX_DISPLAY); }
LogcatPanel::~LogcatPanel() { stop(); }

void LogcatPanel::start(const std::string& deviceSerial) {
    stop();
    m_pausedDeviceSerial = deviceSerial;
    m_displayEntries.clear();
    m_lastPushed = 0; m_lastRefreshTime = -1.0;
    m_selectedIndex = -1; m_showDetail = false; m_contextIndex = -1;
    m_autoScroll = true; m_userScrolledSinceDisable = false; m_pendingNew = 0; m_paused = false;
    m_reader.start(deviceSerial, m_logBuffer);
}
void LogcatPanel::stop() { m_reader.stop(); m_paused = false; }
bool LogcatPanel::isRunning() const { return m_reader.isRunning() || m_paused; }
void LogcatPanel::pause() { if (!m_reader.isRunning()) return; m_reader.stop(); m_paused = true; }
void LogcatPanel::resume() { if (!m_paused) return; m_paused = false; m_reader.start(m_pausedDeviceSerial, m_logBuffer); }
void LogcatPanel::clearLogs() {
    m_logBuffer.clear(); m_displayEntries.clear();
    m_lastPushed = 0; m_lastRefreshTime = -1.0; m_selectedIndex = -1; m_pendingNew = 0;
    m_showDetail = false; m_contextIndex = -1;
    m_userScrolledSinceDisable = false;
}
void LogcatPanel::onDeviceDisconnected() { stop(); clearLogs(); }
bool LogcatPanel::filtersChanged() const {
    return m_lastTextFilter != m_filterBar.textFilter()
        || m_lastTagFilter != m_filterBar.tagFilter()
        || m_lastLevelMask != m_filterBar.levelMask();
}
bool LogcatPanel::dataChanged() const { return m_logBuffer.totalPushed() != m_lastPushed; }

void LogcatPanel::refreshDisplay() {
    std::vector<LogEntry> newEntries;
    newEntries.reserve(MAX_DISPLAY);
    m_logBuffer.query(newEntries,
        m_filterBar.textFilter(), m_filterBar.tagFilter(),
        m_filterBar.levelMask(), MAX_DISPLAY);

    // Enrich with process names
    if (m_dm) {
        for (auto& e : newEntries) {
            if (e.pid > 0) e.processName = m_dm->processName(e.pid);
        }
    }

    m_displayEntries.swap(newEntries);

    if (m_selectedIndex >= (int)m_displayEntries.size())
        m_selectedIndex = m_displayEntries.empty() ? -1 : (int)m_displayEntries.size() - 1;
    m_lastTextFilter = m_filterBar.textFilter();
    m_lastTagFilter = m_filterBar.tagFilter();
    m_lastLevelMask = m_filterBar.levelMask();
    m_lastPushed = m_logBuffer.totalPushed();
    m_lastRefreshTime = ImGui::GetTime();
    m_pendingNew = 0;
}

void LogcatPanel::enableAutoScroll() {
    m_autoScroll = true;
    m_selectedIndex = -1; // live view and a frozen selection are mutually exclusive
    m_userScrolledSinceDisable = false;
    refreshDisplay();
}

// ── helpers ──
static ImVec4 levelTextColor(char lvl) {
    switch (lvl) {
        case 'V': return ImVec4(0.60f,0.60f,0.60f,1.0f);
        case 'D': return ImVec4(0.80f,0.80f,0.80f,1.0f);
        case 'I': return ImVec4(0.35f,0.90f,0.35f,1.0f);
        case 'W': return ImVec4(1.00f,0.80f,0.05f,1.0f);
        case 'E': return ImVec4(1.00f,0.30f,0.30f,1.0f);
        case 'F': return ImVec4(1.00f,0.08f,1.00f,1.0f);
        default:  return ImVec4(0.70f,0.70f,0.70f,1.0f);
    }
}
static ImU32 levelBgColor(char lvl) {
    switch (lvl) {
        case 'V': return IM_COL32(100,100,100,255);
        case 'D': return IM_COL32(50,100,200,255);
        case 'I': return IM_COL32(30,140,30,255);
        case 'W': return IM_COL32(180,140,15,255);
        case 'E': return IM_COL32(190,40,40,255);
        case 'F': return IM_COL32(150,15,150,255);
        default:  return IM_COL32(100,100,100,255);
    }
}

static bool IsMouseOverVerticalScrollbar() {
    ImVec2 wPos = ImGui::GetWindowPos();
    ImVec2 wSize = ImGui::GetWindowSize();
    ImVec2 mouse = ImGui::GetIO().MousePos;
    float sb = ImGui::GetStyle().ScrollbarSize;
    float hSb = (ImGui::GetScrollMaxX() > 0.0f) ? sb : 0.0f; // horizontal bar sits at the bottom
    return mouse.x >= wPos.x + wSize.x - sb - 4.0f
        && mouse.x <= wPos.x + wSize.x
        && mouse.y >= wPos.y + 2.0f
        && mouse.y <= wPos.y + wSize.y - hSb - 2.0f;
}

static const char* LevelName(char lvl) {
    switch (lvl) {
        case 'V': return "Verbose";
        case 'D': return "Debug";
        case 'I': return "Info";
        case 'W': return "Warning";
        case 'E': return "Error";
        case 'F': return "Fatal";
        default:  return "Unknown";
    }
}

static void RenderDetailPanel(const LogEntry& entry) {
    const ImVec4 labelCol(0.57f, 0.60f, 0.64f, 1.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(labelCol, "Time   "); ImGui::SameLine(95);
    if (entry.raw.size() >= 18) ImGui::Text("%.18s", entry.raw.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(labelCol, "Level  "); ImGui::SameLine(95);
    ImGui::TextColored(levelTextColor(entry.level), "%c (%s)", entry.level, LevelName(entry.level));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(labelCol, "PID/TID"); ImGui::SameLine(95);
    ImGui::Text("%d / %d", entry.pid, entry.tid);
    if (!entry.processName.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(labelCol, "Process"); ImGui::SameLine(95);
        ImGui::TextColored(ImVec4(0.72f, 0.72f, 0.32f, 1.0f), "%s", entry.processName.c_str());
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(labelCol, "Tag    "); ImGui::SameLine(95);
    ImGui::TextColored(ImVec4(0.35f, 0.78f, 1.0f, 1.0f), "%s", entry.tag.c_str());
    ImGui::Separator();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Message");
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(entry.message.c_str());
    ImGui::SameLine(); ImGui::TextDisabled("(select text in Raw Line to copy)");
    ImGui::BeginChild("##detailMsgScroll", ImVec2(0, -54), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped("%s", entry.message.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();

    if (ImGui::CollapsingHeader("Raw Line (selectable, Ctrl+C to copy)")) {
        ImGui::InputTextMultiline("##detailRaw", const_cast<char*>(entry.raw.c_str()),
            entry.raw.size() + 1, ImVec2(-FLT_MIN, 60.0f), ImGuiInputTextFlags_ReadOnly);
    }
}

// ── main render ──
void LogcatPanel::render(Settings& settings) {
    // ── Filter row ──
    m_filterBar.render(settings);

    // ── Action row ──
    if (ImGui::Checkbox("Auto-scroll", &m_autoScroll)) {
        if (m_autoScroll) enableAutoScroll();
        else m_userScrolledSinceDisable = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Go Bottom")) enableAutoScroll();
    ImGui::SameLine();
    if (m_paused) { if (ImGui::Button("Resume")) resume(); }
    else          { if (ImGui::Button("Pause"))  pause(); }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) clearLogs();
    ImGui::SameLine();
    if (ImGui::Button(m_showDetail ? "Hide Details" : "Show Details")) m_showDetail = !m_showDetail;

    // ── Stats ──
    ImGui::SameLine();
    size_t total = m_logBuffer.size();
    ImGui::TextColored(ImVec4(0.57f, 0.60f, 0.64f, 1.0f),
        "Total: %zu / %zu | Filtered: %zu", total, m_logBuffer.DEFAULT_CAPACITY, m_displayEntries.size());
    if (m_pendingNew > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.25f, 1.0f), "| +%zu new", m_pendingNew);
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.48f, 0.50f, 0.54f, 1.0f),
        "| Lines: %lld/%lld", (long long)m_reader.parsedLines(), (long long)m_reader.totalLines());
    if (m_paused) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1,0.85f,0.2f,1), "[PAUSED]"); }
    if (m_logBuffer.overflowed()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1,0.6f,0.15f,1), "[buffer full]"); }
    ImGui::Separator();

    // ── Refresh ──
    if (filtersChanged()) refreshDisplay();
    else if (m_autoScroll && m_selectedIndex < 0 && !m_paused && dataChanged()) {
        double now = ImGui::GetTime();
        if (m_lastRefreshTime < 0.0 || (now - m_lastRefreshTime) >= MIN_REFRESH_INTERVAL)
            refreshDisplay();
    }
    else if (!m_paused && dataChanged()) {
        size_t nt = m_logBuffer.totalPushed();
        if (nt > m_lastPushed) { m_pendingNew += (nt - m_lastPushed); m_lastPushed = nt; }
    }

    float totalAvail = ImGui::GetContentRegionAvail().y;
    float detailH = (m_showDetail && m_selectedIndex >= 0) ? 230.0f : 0.0f;
    float logH = totalAvail - detailH - (detailH > 0.0f ? 8.0f : 0.0f);
    if (logH < 80.0f) logH = 80.0f;

    // ── Log view ──
    ImGui::BeginChild("LogView", ImVec2(0, logH), true);

    int totalRows = (int)m_displayEntries.size();
    bool deselectThisFrame = false;
    bool rowRightClicked = false;
    float rowH = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().CellPadding.y * 2.0f;

    if (ImGui::BeginTable("##logTable", 5,
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Lvl", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 182.0f);
        ImGui::TableSetupColumn("PID/TID", ImGuiTableColumnFlags_WidthFixed, 112.0f);
        ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(totalRows, rowH);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                const auto& entry = m_displayEntries[i];
                bool isSel = (i == m_selectedIndex);
                ImGui::PushID(i);
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);

                if (isSel)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(36, 84, 54, 255));

                // Row interaction (selectable spans all columns)
                ImGui::TableSetColumnIndex(0);
                ImVec2 cell0Pos = ImGui::GetCursorScreenPos(); // capture before selectable
                if (ImGui::Selectable("##row", isSel, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, rowH))) {
                    if (i == m_selectedIndex) {
                        m_selectedIndex = -1;   // deselect → unfreeze and show latest
                        deselectThisFrame = true;
                    } else {
                        m_selectedIndex = i;    // select → freeze + show detail
                        m_showDetail = true;
                        m_autoScroll = false;
                        m_userScrolledSinceDisable = false;
                    }
                }
                if (ImGui::IsItemHovered() && !isSel)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(58, 64, 74, 255));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    m_contextIndex = i;
                    rowRightClicked = true;
                }

                // Level badge + char
                ImGui::SetCursorScreenPos(cell0Pos); // selectable advanced the cursor; snap back
                {
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    float bh = ImGui::GetTextLineHeight();
                    float bw = bh * 0.8f;
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        p, ImVec2(p.x + bw, p.y + bh), levelBgColor(entry.level), 3.0f);
                    char lvl[2] = {entry.level, 0};
                    ImVec2 tsz = ImGui::CalcTextSize(lvl);
                    ImGui::SetCursorScreenPos(ImVec2(p.x + (bw - tsz.x) * 0.5f, p.y));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                    ImGui::TextUnformatted(lvl);
                    ImGui::PopStyleColor();
                }

                // Time
                ImGui::TableSetColumnIndex(1);
                if (entry.raw.size() >= 18)
                    ImGui::TextUnformatted(entry.raw.c_str(), entry.raw.c_str() + 18);

                // PID/TID
                ImGui::TableSetColumnIndex(2);
                char pt[32];
                snprintf(pt, sizeof(pt), "%d/%d", entry.pid, entry.tid);
                ImGui::TextUnformatted(pt);

                // Tag
                ImGui::TableSetColumnIndex(3);
                if (!entry.tag.empty())
                    ImGui::TextColored(ImVec4(0.35f, 0.78f, 1.0f, 1.0f), "%s", entry.tag.c_str());

                // Message (clipped to the column)
                ImGui::TableSetColumnIndex(4);
                {
                    ImVec2 cMin = ImGui::GetCursorScreenPos();
                    float cw = ImGui::GetColumnWidth();
                    ImGui::PushClipRect(cMin, ImVec2(cMin.x + cw, cMin.y + rowH), true);
                    ImGui::TextColored(levelTextColor(entry.level), "%s",
                        entry.tag.empty() ? entry.raw.c_str() : entry.message.c_str());
                    ImGui::PopClipRect();
                }
                ImGui::PopID();
            }
        }
        clipper.End();

        // ── Auto-scroll state machine (targets the table's scroll window) ──
        bool userWheelUp = ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel > 0.01f;
        if (!m_autoScroll && ImGui::IsWindowHovered()) {
            bool wheelActive = ImGui::GetIO().MouseWheel != 0.0f;
            bool scrollbarHeld = ImGui::IsMouseDown(ImGuiMouseButton_Left) && IsMouseOverVerticalScrollbar();
            if (wheelActive || scrollbarHeld)
                m_userScrolledSinceDisable = true;
        }
        bool justEnabled = false;
        if (!m_autoScroll && totalRows > 0) {
            float smy = ImGui::GetScrollMaxY();
            bool atBottom = smy <= 0.0f || ImGui::GetScrollY() >= smy - 2.0f;
            bool enter = ImGui::IsWindowHovered()
                && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
            if ((atBottom && m_userScrolledSinceDisable && !userWheelUp) || enter) {
                enableAutoScroll();
                justEnabled = true;
            }
        }
        if (m_autoScroll && !justEnabled && ImGui::IsWindowHovered()) {
            bool wheelUp = ImGui::GetIO().MouseWheel > 0.01f;
            bool scrollbarGrabbed = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && IsMouseOverVerticalScrollbar();
            if (wheelUp || scrollbarGrabbed) {
                m_autoScroll = false;
                m_userScrolledSinceDisable = false;
            }
        }
        if (m_autoScroll && totalRows > 0) {
            float maxY = ImGui::GetScrollMaxY();
            if (maxY > 0.0f)
                ImGui::SetScrollY(maxY);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    if (deselectThisFrame) refreshDisplay();

    // ── Row context menu ──
    if (rowRightClicked) ImGui::OpenPopup("LogRowContext");
    if (ImGui::BeginPopup("LogRowContext")) {
        if (m_contextIndex >= 0 && m_contextIndex < (int)m_displayEntries.size()) {
            const auto& e = m_displayEntries[m_contextIndex];
            if (ImGui::MenuItem("Copy Message")) ImGui::SetClipboardText(e.message.c_str());
            if (ImGui::MenuItem("Copy Raw Line")) ImGui::SetClipboardText(e.raw.c_str());
            if (!e.tag.empty() && ImGui::MenuItem("Copy Tag")) ImGui::SetClipboardText(e.tag.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("Clear Logs")) clearLogs();
        }
        ImGui::EndPopup();
    }

    // ── Detail panel ──
    if (m_showDetail && m_selectedIndex >= 0 && m_selectedIndex < totalRows) {
        ImGui::Separator();
        ImGui::BeginChild("DetailPanel", ImVec2(0, detailH), true);
        RenderDetailPanel(m_displayEntries[m_selectedIndex]);
        ImGui::EndChild();
    }
}
