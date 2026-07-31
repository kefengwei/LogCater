#include "AdbProcess.h"
#include <cstdio>
#include <array>

static std::string g_adbPath;

std::string AdbProcess::findAdbPath() {
    if (!g_adbPath.empty()) return g_adbPath;

#ifdef ADB_PATH
    g_adbPath = ADB_PATH;
    return g_adbPath;
#endif

    // Check Android SDK default location
    char buf[MAX_PATH];
    if (GetEnvironmentVariableA("LOCALAPPDATA", buf, sizeof(buf))) {
        std::string sdkPath = std::string(buf) + "\\Android\\Sdk\\platform-tools\\adb.exe";
        if (GetFileAttributesA(sdkPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            g_adbPath = sdkPath;
            return g_adbPath;
        }
    }

    // Check bundled adb (next to the executable)
    if (GetModuleFileNameA(nullptr, buf, sizeof(buf))) {
        std::string exeDir(buf);
        size_t lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            exeDir = exeDir.substr(0, lastSlash);
        }
        std::string bundledPath = exeDir + "\\adb\\adb.exe";
        if (GetFileAttributesA(bundledPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            g_adbPath = bundledPath;
            return g_adbPath;
        }
    }

    // Try PATH
    g_adbPath = "adb.exe";
    return g_adbPath;
}

AdbProcess::AdbProcess() = default;

AdbProcess::~AdbProcess() {
    stop();
}

bool AdbProcess::start(const std::vector<std::string>& args, LineCallback onLine) {
    return startWithInput(args, std::move(onLine), "");
}

bool AdbProcess::startWithInput(const std::vector<std::string>& args, LineCallback onLine,
                                 const std::string& stdinData) {
    if (m_running.load()) return false;

    m_callback = std::move(onLine);

    // Build command line
    std::string cmdLine = findAdbPath();
    for (const auto& arg : args) {
        cmdLine += " ";
        if (arg.find(' ') != std::string::npos) {
            cmdLine += "\"" + arg + "\"";
        } else {
            cmdLine += arg;
        }
    }

    SECURITY_ATTRIBUTES saAttr = {};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = nullptr;

    // Create pipe for stdout
    if (!CreatePipe(&m_hStdoutRead, &m_hStdoutWrite, &saAttr, 0)) {
        return false;
    }
    SetHandleInformation(m_hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    // Create pipe for stdin (if input data provided)
    bool hasInput = !stdinData.empty();
    if (hasInput) {
        if (!CreatePipe(&m_hStdinRead, &m_hStdinWrite, &saAttr, 0)) {
            CloseHandle(m_hStdoutRead);
            CloseHandle(m_hStdoutWrite);
            m_hStdoutRead = nullptr;
            m_hStdoutWrite = nullptr;
            return false;
        }
        SetHandleInformation(m_hStdinWrite, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOA si = {};
    si.cb = sizeof(STARTUPINFOA);
    si.hStdOutput = m_hStdoutWrite;
    si.hStdError = m_hStdoutWrite;
    si.hStdInput = hasInput ? m_hStdinRead : GetStdHandle(STD_INPUT_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};

    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    if (!CreateProcessA(
            nullptr, cmdBuf.data(), nullptr, nullptr,
            TRUE,              // inherit handles
            CREATE_NO_WINDOW,
            nullptr, nullptr, &si, &pi)) {
        CloseHandle(m_hStdoutRead);
        CloseHandle(m_hStdoutWrite);
        m_hStdoutRead = nullptr;
        m_hStdoutWrite = nullptr;
        if (hasInput) {
            CloseHandle(m_hStdinRead);
            CloseHandle(m_hStdinWrite);
            m_hStdinRead = nullptr;
            m_hStdinWrite = nullptr;
        }
        return false;
    }

    CloseHandle(m_hStdoutWrite);
    m_hStdoutWrite = nullptr;

    // Write stdin data and close the write end (signals EOF to child)
    if (hasInput) {
        CloseHandle(m_hStdinRead);
        m_hStdinRead = nullptr;

        DWORD written = 0;
        WriteFile(m_hStdinWrite, stdinData.data(),
                  static_cast<DWORD>(stdinData.size()), &written, nullptr);
        CloseHandle(m_hStdinWrite);
        m_hStdinWrite = nullptr;
    }

    m_hProcess = pi.hProcess;
    CloseHandle(pi.hThread);

    m_running.store(true);
    m_readerThread = std::thread(&AdbProcess::readerLoop, this);

    return true;
}

void AdbProcess::stop() {
    if (!m_running.load()) return;
    m_running.store(false);

    // Terminate the process
    if (m_hProcess) {
        TerminateProcess(m_hProcess, 0);
        CloseHandle(m_hProcess);
        m_hProcess = nullptr;
    }

    // Close pipe handles
    if (m_hStdoutRead) {
        CloseHandle(m_hStdoutRead);
        m_hStdoutRead = nullptr;
    }
    if (m_hStdoutWrite) {
        CloseHandle(m_hStdoutWrite);
        m_hStdoutWrite = nullptr;
    }
    if (m_hStdinRead) {
        CloseHandle(m_hStdinRead);
        m_hStdinRead = nullptr;
    }
    if (m_hStdinWrite) {
        CloseHandle(m_hStdinWrite);
        m_hStdinWrite = nullptr;
    }

    if (m_readerThread.joinable()) {
        m_readerThread.join();
    }
}

bool AdbProcess::waitForExit(DWORD timeoutMs) {
    if (!m_hProcess) return true;
    DWORD result = WaitForSingleObject(m_hProcess, timeoutMs);
    if (result == WAIT_OBJECT_0) {
        CloseHandle(m_hProcess);
        m_hProcess = nullptr;
        // Give reader thread a moment to drain remaining pipe data
        Sleep(200);
        stop();
        return true;
    }
    return false; // timeout
}

void AdbProcess::readerLoop() {
    std::array<char, 4096> buf;
    std::string pending;

    while (m_running.load()) {
        DWORD bytesRead = 0;
        if (!ReadFile(m_hStdoutRead, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead, nullptr)) {
            break; // pipe closed or error
        }
        if (bytesRead == 0) {
            break;
        }

        pending.append(buf.data(), bytesRead);

        // Extract complete lines
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);

            // Trim trailing \r
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (m_callback) {
                m_callback(line);
            }
        }
    }

    // Don't call callback here as we're shutting down
}
