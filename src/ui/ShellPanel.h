#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>

/// ADB shell terminal — run adb shell commands, show output, keep history.
class ShellPanel {
public:
    ShellPanel();

    void render(const std::string& deviceSerial);

private:
    std::vector<std::string> m_history;     // command history (oldest first)
    int m_historyIndex = -1;                // -1 = editing new command
    char m_inputBuf[1024] = {};
    char m_draftBuf[1024] = {};             // saved draft while navigating history

    // Output area
    std::vector<std::string> m_lines;
    std::mutex m_mutex;
    std::atomic<bool> m_running{false};
    int m_autoScrollCount = 0;

    void execute(const std::string& deviceSerial, const std::string& cmd);
    void appendLine(const std::string& line);
    void renderInput(const std::string& deviceSerial);
};
