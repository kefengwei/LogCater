#include "ShellPanel.h"
#include "core/AdbProcess.h"
#include "imgui.h"
#include <thread>
#include <cstring>

ShellPanel::ShellPanel() = default;

void ShellPanel::appendLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lines.push_back(line);
    m_autoScrollCount++;
}

void ShellPanel::execute(const std::string& deviceSerial, const std::string& cmd) {
    if (deviceSerial.empty() || cmd.empty() || m_running.load()) return;

    // Trim
    size_t b = cmd.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return;
    std::string trimmed = cmd.substr(b);

    // Add to history (dedupe consecutive)
    if (m_history.empty() || m_history.back() != trimmed) {
        m_history.push_back(trimmed);
        if (m_history.size() > 200) m_history.erase(m_history.begin());
    }
    m_historyIndex = -1;

    appendLine("");
    appendLine("$ adb -s " + deviceSerial + " shell " + trimmed);
    m_running.store(true);

    std::thread([this, deviceSerial, trimmed]() {
        AdbProcess proc;
        std::vector<std::string> pending;
        proc.start({"-s", deviceSerial, "shell", trimmed},
            [this, &pending](const std::string& line) {
                std::lock_guard<std::mutex> lock(m_mutex);
                pending.push_back(line);
            });
        proc.waitForExit(10000);
        proc.stop();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& l : pending) m_lines.push_back(l);
            m_lines.push_back("[exit]");
            m_autoScrollCount += static_cast<int>(pending.size()) + 1;
        }
        m_running.store(false);
    }).detach();
}

void ShellPanel::renderInput(const std::string& deviceSerial) {
    ImGui::TextUnformatted(">");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                ImGuiInputTextFlags_NoHorizontalScroll;

    // History navigation
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        if (m_historyIndex < 0) {
            std::strncpy(m_draftBuf, m_inputBuf, sizeof(m_draftBuf) - 1);
        }
        if (m_historyIndex + 1 < static_cast<int>(m_history.size())) {
            m_historyIndex++;
            std::strncpy(m_inputBuf, m_history[m_history.size() - 1 - m_historyIndex].c_str(),
                         sizeof(m_inputBuf) - 1);
            m_inputBuf[sizeof(m_inputBuf) - 1] = '\0';
        }
    }
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        if (m_historyIndex > 0) {
            m_historyIndex--;
            std::strncpy(m_inputBuf, m_history[m_history.size() - 1 - m_historyIndex].c_str(),
                         sizeof(m_inputBuf) - 1);
            m_inputBuf[sizeof(m_inputBuf) - 1] = '\0';
        } else if (m_historyIndex == 0) {
            m_historyIndex = -1;
            std::strncpy(m_inputBuf, m_draftBuf, sizeof(m_inputBuf) - 1);
            m_inputBuf[sizeof(m_inputBuf) - 1] = '\0';
        }
    }

    if (ImGui::InputTextWithHint("##shellCmd", "enter adb shell command... (e.g. dumpsys meminfo)",
                                 m_inputBuf, sizeof(m_inputBuf), flags)) {
        // Enter pressed
        std::string cmd(m_inputBuf);
        m_inputBuf[0] = '\0';
        execute(deviceSerial, cmd);
    }
}

void ShellPanel::render(const std::string& deviceSerial) {
    // Quick command buttons
    static const char* quickCmds[] = {
        "dumpsys meminfo", "dumpsys cpuinfo", "dumpsys battery",
        "getprop", "ps -A", "df -h", "free", "uptime"
    };
    for (const char* c : quickCmds) {
        if (ImGui::SmallButton(c)) {
            execute(deviceSerial, c);
        }
        ImGui::SameLine();
    }
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                       m_running.load() ? "Running..." : "Ready");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Commands time out after 10s");

    ImGui::Separator();

    // Output view (virtual scrolled)
    ImGui::BeginChild("ShellOutput", ImVec2(0, -28), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    std::vector<std::string> snapshot;
    int scrollCount;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        snapshot = m_lines;
        scrollCount = m_autoScrollCount;
    }
    if (scrollCount > 0) {
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
        std::lock_guard<std::mutex> lock(m_mutex);
        m_autoScrollCount = 0;
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(snapshot.size()), ImGui::GetTextLineHeightWithSpacing());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            const std::string& line = snapshot[i];
            bool isCmd = !line.empty() && line[0] == '$';
            bool isExit = line == "[exit]";
            if (isCmd)
                ImGui::TextColored(ImVec4(0.35f, 0.8f, 1.0f, 1.0f), "%s", line.c_str());
            else if (isExit)
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", line.c_str());
            else
                ImGui::TextUnformatted(line.c_str());
        }
    }
    ImGui::EndChild();

    // Input row (fixed at bottom)
    renderInput(deviceSerial);
}
