#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <Windows.h>

/// Low-level Windows subprocess wrapper.
/// Spawns a process with stdout piped, reads lines on a dedicated thread.
class AdbProcess {
public:
    using LineCallback = std::function<void(const std::string& line)>;

    AdbProcess();
    ~AdbProcess();

    /// Start an adb command. Returns true if process spawned successfully.
    bool start(const std::vector<std::string>& args, LineCallback onLine);

    /// Start an adb command and write input to its stdin, then close stdin.
    /// Used for interactive commands like 'adb pair' that read from stdin.
    bool startWithInput(const std::vector<std::string>& args, LineCallback onLine,
                        const std::string& stdinData);

    /// Kill process and join reader thread.
    void stop();

    /// Wait for process to exit naturally, with timeout in ms. Returns true if exited.
    bool waitForExit(DWORD timeoutMs = 10000);

    bool isRunning() const { return m_running.load(); }

private:
    HANDLE m_hProcess = nullptr;
    HANDLE m_hStdoutRead = nullptr;
    HANDLE m_hStdoutWrite = nullptr;
    HANDLE m_hStdinRead = nullptr;
    HANDLE m_hStdinWrite = nullptr;
    std::thread m_readerThread;
    std::atomic<bool> m_running{false};
    LineCallback m_callback;

    void readerLoop();
    static std::string findAdbPath();
};
