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
    m_lastBufferTotal = 0; m_lastRefreshTime = -1.0;
    m_selectedIndex = -1;
    m_autoScroll = true; m_pendingNew = 0; m_paused = false;
    m_reader.start(deviceSerial, m_logBuffer);
}
void LogcatPanel::stop() { m_reader.stop(); m_paused = false; }
bool LogcatPanel::isRunning() const { return m_reader.isRunning() || m_paused; }
void LogcatPanel::pause() { if (!m_reader.isRunning()) return; m_reader.stop(); m_paused = true; }
void LogcatPanel::resume() { if (!m_paused) return; m_paused = false; m_reader.start(m_pausedDeviceSerial, m_logBuffer); }
void LogcatPanel::clearLogs() {
    m_logBuffer.clear(); m_displayEntries.clear();
    m_lastBufferTotal = 0; m_lastRefreshTime = -1.0; m_selectedIndex = -1; m_pendingNew = 0;
}
void LogcatPanel::onDeviceDisconnected() { stop(); clearLogs(); }
bool LogcatPanel::filtersChanged() const {
    return m_lastTextFilter != m_filterBar.textFilter()
        || m_lastTagFilter != m_filterBar.tagFilter()
        || m_lastLevelMask != m_filterBar.levelMask();
}
bool LogcatPanel::dataChanged() const { return m_logBuffer.size() != m_lastBufferTotal; }

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
    m_lastBufferTotal = m_logBuffer.size();
    m_lastRefreshTime = ImGui::GetTime();
    m_pendingNew = 0;
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

static void RenderDetailPanel(const LogEntry& entry) {
    ImGui::Text("Time : "); ImGui::SameLine(70);
    if (entry.raw.size() >= 18) ImGui::Text("%.18s", entry.raw.c_str());
    ImGui::Text("Level:"); ImGui::SameLine(70);
    ImGui::TextColored(levelTextColor(entry.level), "%c", entry.level);
    ImGui::Text("PID  :"); ImGui::SameLine(70); ImGui::Text("%d", entry.pid);
    ImGui::Text("TID  :"); ImGui::SameLine(70); ImGui::Text("%d", entry.tid);
    ImGui::Text("Tag  :"); ImGui::SameLine(70);
    ImGui::TextColored(ImVec4(0.0f,0.85f,1.0f,1.0f), "%s", entry.tag.c_str());
    ImGui::Separator();

    ImGui::TextUnformatted("Message:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(entry.message.c_str());
    ImGui::SameLine(); ImGui::TextDisabled("(use Raw Line for select+copy)");
    ImGui::BeginChild("##detailMsgScroll", ImVec2(0, -4), false, ImGuiWindowFlags_HorizontalScrollbar);
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
    m_filterBar.render(settings);

    // Auto-scroll checkbox
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_autoScroll);

    // Go Bottom
    ImGui::SameLine();
    if (ImGui::SmallButton("Go Bottom")) {
        m_autoScroll = true;
        refreshDisplay();
    }

    // Pause / Resume / Clear
    ImGui::SameLine();
    if (m_paused) { if (ImGui::Button("Resume")) resume(); }
    else          { if (ImGui::Button("Pause")) pause(); }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) clearLogs();

    // Stats
    ImGui::SameLine();
    size_t total = m_logBuffer.size();
    ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.f),
        "Total: %zu / %zu | Filtered: %zu", total, m_logBuffer.DEFAULT_CAPACITY, m_displayEntries.size());
    if (m_pendingNew > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.f,0.8f,0.2f,1.f), "| +%zu new", m_pendingNew);
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.f),
        "| Lines: %lld/%lld", (long long)m_reader.parsedLines(), (long long)m_reader.totalLines());
    if (m_paused) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1,0.8f,0,1), "[PAUSED]"); }
    if (m_logBuffer.overflowed()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1,0.5f,0,1), "[buffer full]"); }
    ImGui::Separator();

    // ── Refresh ──
    if (filtersChanged()) refreshDisplay();
    else if (m_autoScroll && m_selectedIndex < 0 && !m_paused && dataChanged()) {
        double now = ImGui::GetTime();
        if (m_lastRefreshTime < 0.0 || (now - m_lastRefreshTime) >= MIN_REFRESH_INTERVAL)
            refreshDisplay();
    }
    else if (!m_paused && dataChanged()) {
        size_t nt = m_logBuffer.size();
        if (nt > m_lastBufferTotal) { m_pendingNew += (nt - m_lastBufferTotal); m_lastBufferTotal = nt; }
    }

    float totalAvail = ImGui::GetContentRegionAvail().y;
    float detailH = (m_selectedIndex >= 0) ? totalAvail * 0.35f : 0.0f;
    float logH = (m_selectedIndex >= 0) ? totalAvail - detailH - 4.0f : 0.0f;

    // ── Log view ──
    ImGui::BeginChild("LogView", ImVec2(0, (m_selectedIndex >= 0) ? logH : 0),
                      false, ImGuiWindowFlags_HorizontalScrollbar);

    int totalRows = (int)m_displayEntries.size();
    ImGuiListClipper clipper;
    clipper.Begin(totalRows, ImGui::GetTextLineHeightWithSpacing());

    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            const auto& entry = m_displayEntries[i];
            bool isSel = (i == m_selectedIndex);

            ImVec2 rowStart = ImGui::GetCursorScreenPos();
            float rowH = ImGui::GetTextLineHeightWithSpacing();
            float rowW = ImGui::GetContentRegionAvail().x;

            if (isSel)
                ImGui::GetWindowDrawList()->AddRectFilled(
                    rowStart, ImVec2(rowStart.x+rowW, rowStart.y+rowH), IM_COL32(60,60,30,200));

            ImGui::PushID(i);
            ImGui::InvisibleButton("##row", ImVec2(rowW, rowH));
            if (ImGui::IsItemClicked())
                m_selectedIndex = (i == m_selectedIndex) ? -1 : i;
            ImGui::PopID();

            ImGui::SetCursorScreenPos(rowStart);
            float bh = ImGui::GetTextLineHeight(), bw = bh * 0.65f;
            ImGui::GetWindowDrawList()->AddRectFilled(rowStart, ImVec2(rowStart.x+bw, rowStart.y+bh), levelBgColor(entry.level), 3.f);
            char lvlStr[2]={entry.level,0};
            ImVec2 tsz = ImGui::CalcTextSize(lvlStr);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,1));
            ImGui::SetCursorScreenPos(ImVec2(rowStart.x+(bw-tsz.x)*0.5f, rowStart.y));
            ImGui::TextUnformatted(lvlStr);
            ImGui::PopStyleColor();
            ImGui::SetCursorScreenPos(ImVec2(rowStart.x+bw+5, rowStart.y));

            ImVec4 lvlCol = levelTextColor(entry.level);
            const char* raw = entry.raw.c_str();
            if (!entry.tag.empty()) {
                size_t tagPos = entry.raw.find(entry.tag);
                if (tagPos != std::string::npos && tagPos > 0) {
                    ImGui::TextColored(ImVec4(.55f,.55f,.55f,1), "%.*s", (int)tagPos, raw);
                    ImGui::SameLine(0,0);
                    ImGui::TextColored(ImVec4(0,.85f,1,1), "%22s", entry.tag.c_str());
                    ImGui::SameLine(0,0);
                    if (!entry.processName.empty()) {
                        ImGui::TextColored(ImVec4(0.7f,0.7f,0.3f,1), "%25s", entry.processName.c_str());
                        ImGui::SameLine(0,0);
                    }
                    ImGui::TextColored(lvlCol, "%s", raw + tagPos + entry.tag.size());
                } else ImGui::TextColored(lvlCol, "%s", raw);
            } else ImGui::TextColored(lvlCol, "%s", raw);
        }
    }

    // ── Scroll to bottom when auto-scrolling ──
    if (m_autoScroll && totalRows > 0) {
        float maxY = ImGui::GetScrollMaxY();
        if (maxY > 0.0f)
            ImGui::SetScrollY(maxY);
    }

    // Detect user scrolling up via mouse wheel → turn off auto-scroll
    if (m_autoScroll && ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel > 0.01f) { // scroll up
            m_autoScroll = false;
        }
    }
    // Detect user dragging scrollbar to bottom → turn on auto-scroll
    if (!m_autoScroll && totalRows > 0) {
        float scrollMaxY = ImGui::GetScrollMaxY();
        if (scrollMaxY > 0.0f && ImGui::GetScrollY() >= scrollMaxY - 2.0f) {
            m_autoScroll = true;
        }
    }

    clipper.End();
    ImGui::EndChild();

    // ── Detail panel ──
    if (m_selectedIndex >= 0 && m_selectedIndex < totalRows) {
        ImGui::Separator();
        ImGui::BeginChild("DetailPanel", ImVec2(0, detailH), true);
        RenderDetailPanel(m_displayEntries[m_selectedIndex]);
        ImGui::EndChild();
    }
}
