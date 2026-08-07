#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <cstdint>
#include <regex>
#include <vector>

class AdbProcess;
class LogBuffer;
struct LogEntry;

/// Streams "adb logcat -v threadtime" into a LogBuffer on a background thread.
class LogcatReader {
public:
    LogcatReader();
    ~LogcatReader();

    /// Start streaming from the selected device.
    /// @param bufferName  "main" | "system" | "crash" | "all" | "" (default, merged main+system+crash)
    void start(const std::string& deviceSerial, LogBuffer& buffer,
               const std::string& bufferName = "");

    /// Stop streaming and join reader thread.
    void stop();

    bool isRunning() const { return m_running.load(); }

    int64_t totalLines() const { return m_totalLines.load(); }
    int64_t parsedLines() const { return m_parsedLines.load(); }
    int64_t skippedLines() const { return m_skippedLines.load(); }

private:
    std::unique_ptr<AdbProcess> m_process;
    LogBuffer* m_buffer = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<int64_t> m_entryIndex{0};
    std::atomic<int64_t> m_totalLines{0};
    std::atomic<int64_t> m_parsedLines{0};
    std::atomic<int64_t> m_skippedLines{0};
    std::regex m_lineRegex;

    void onLogLine(const std::string& rawLine);
    static LogEntry parseLine(const std::string& line, int64_t index, std::regex& re);
};
