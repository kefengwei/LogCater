#include "DeviceInfoPanel.h"
#include "core/AdbProcess.h"
#include "core/Settings.h"
#include "imgui.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <GL/gl.h>
#include <shellapi.h>
#include <algorithm>
#include <regex>
#include <sstream>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
#include <chrono>
#include <functional>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

static ULONG_PTR g_gdiplusToken = 0;

DeviceInfoPanel::DeviceInfoPanel() {
    GdiplusStartupInput in;
    GdiplusStartup(&g_gdiplusToken, &in, nullptr);
}

DeviceInfoPanel::~DeviceInfoPanel() {
    destroyPreview();
    if (g_gdiplusToken) {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

// ─── Screen capture helpers ───────────────────────────────────────

namespace {

std::string timestampName(const char* prefix, const char* ext) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[MAX_PATH];
    std::snprintf(buf, sizeof(buf), "%s_%04d%02d%02d_%02d%02d%02d.%s",
                  prefix, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, ext);
    return std::string(buf);
}

} // namespace

// ─── Capture / cache helpers ─────────────────────────────────────

std::string DeviceInfoPanel::appDataBase() {
    std::string p = Settings::defaultPath(); // ...\LogCater\settings.json
    size_t pos = p.find_last_of("\\/");
    return pos == std::string::npos ? "LogCater" : p.substr(0, pos);
}

std::string DeviceInfoPanel::capturesDirFor(const std::string& serial) {
    std::string safe = serial;
    std::replace(safe.begin(), safe.end(), ':', '_');
    std::string base = appDataBase() + "\\captures";
    CreateDirectoryA(base.c_str(), nullptr);
    std::string dir = base + "\\" + safe;
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

void DeviceInfoPanel::destroyPreview() {
    if (m_previewTexture) {
        glDeleteTextures(1, &m_previewTexture);
        m_previewTexture = 0;
    }
    m_previewW = m_previewH = 0;
}

void DeviceInfoPanel::loadPreview(const std::string& path) {
    destroyPreview();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return;
    std::wstring wpath(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], wlen);

    Bitmap bmp(wpath.c_str());
    if (bmp.GetLastStatus() != Ok) return;
    m_previewW = bmp.GetWidth();
    m_previewH = bmp.GetHeight();
    if (m_previewW <= 0 || m_previewH <= 0) return;

    Rect r(0, 0, m_previewW, m_previewH);
    BitmapData data;
    if (bmp.LockBits(&r, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok) return;

    glGenTextures(1, &m_previewTexture);
    glBindTexture(GL_TEXTURE_2D, m_previewTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_previewW, m_previewH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data.Scan0);
    glBindTexture(GL_TEXTURE_2D, 0);
    bmp.UnlockBits(&data);
}

void DeviceInfoPanel::openInExplorer(const std::string& path) {
    ShellExecuteA(nullptr, "open", "explorer.exe",
                  ("/select,\"" + path + "\"").c_str(), nullptr, SW_SHOWNORMAL);
}

void DeviceInfoPanel::scanCaptures(const std::string& deviceSerial) {
    if (deviceSerial.empty()) return;
    std::string dir = capturesDirFor(deviceSerial);
    std::vector<CaptureEntry> entries;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                CaptureEntry e;
                e.name = fd.cFileName;
                e.path = dir + "\\" + fd.cFileName;
                e.sizeBytes = ((int64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                std::string lower = e.name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                e.isImage = lower.find(".png") != std::string::npos;
                entries.push_back(std::move(e));
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(entries.begin(), entries.end(),
              [](const CaptureEntry& a, const CaptureEntry& b) { return a.name > b.name; });
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_captures = std::move(entries);
        if (m_selectedCapture >= (int)m_captures.size()) m_selectedCapture = -1;
        if (m_exportPending >= (int)m_captures.size()) m_exportPending = -1;
    }
}

void DeviceInfoPanel::exportCapture(const std::string& deviceSerial, int index) {
    (void)deviceSerial;
    CaptureEntry e;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (index < 0 || index >= (int)m_captures.size()) return;
        e = m_captures[index];
    }
    char path[MAX_PATH] = {};
    std::snprintf(path, sizeof(path), "%s", e.name.c_str());
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameA(&ofn)) return;
    if (!CopyFileA(e.path.c_str(), path, FALSE)) return;
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_exportPending = index;   // UI asks whether to delete the cache copy
}

void DeviceInfoPanel::deleteCapture(const std::string& deviceSerial, int index) {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (index < 0 || index >= (int)m_captures.size()) return;
        path = m_captures[index].path;
    }
    DeleteFileA(path.c_str());
    scanCaptures(deviceSerial);
}

void DeviceInfoPanel::clearAllCaptures(const std::string& deviceSerial) {
    std::vector<std::string> paths;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        for (auto& c : m_captures) paths.push_back(c.path);
    }
    for (auto& p : paths) DeleteFileA(p.c_str());
    scanCaptures(deviceSerial);
}

void DeviceInfoPanel::scanBugreport(const std::string& deviceSerial) {
    (void)deviceSerial;
    std::string root;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        root = m_bugreportDir;
    }
    if (root.empty()) return;

    std::vector<BugreportFile> files;
    std::function<void(const std::string&)> walk = [&](const std::string& d) {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((d + "\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (fd.cFileName[0] != '.') walk(d + "\\" + fd.cFileName);
            } else {
                std::string name = fd.cFileName;
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                static const char* keys[] = {"tombstone", "anr", "traces", "dropbox",
                                             "dumpstate", "event"};
                bool key = false;
                for (auto k : keys) {
                    if (lower.find(k) != std::string::npos) { key = true; break; }
                }
                if (key) {
                    BugreportFile f;
                    f.name = name;
                    f.path = d + "\\" + name;
                    f.sizeBytes = ((int64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                    files.push_back(std::move(f));
                }
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    };
    walk(root);
    std::sort(files.begin(), files.end(),
              [](const BugreportFile& a, const BugreportFile& b) { return a.name < b.name; });
    if (files.size() > 200) files.resize(200);
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_bugreportFiles = std::move(files);
    }
}

void DeviceInfoPanel::takeScreenshot(const std::string& deviceSerial) {
    if (deviceSerial.empty() || m_actionRunning.load()) return;
    std::string local = capturesDirFor(deviceSerial) + "\\" + timestampName("screenshot", "png");

    m_actionRunning.store(true);
    { std::lock_guard<std::mutex> lock(m_stateMutex); m_actionMsg = "Capturing screenshot..."; }
    m_actionMsgEnd = static_cast<float>(ImGui::GetTime()) + 3.0f;

    std::thread([this, deviceSerial, local]() {
        const std::string remote = "/sdcard/logcater_shot.png";
        {
            AdbProcess p;
            p.start({"-s", deviceSerial, "shell", "screencap", "-p", remote},
                    [](const std::string&) {});
            p.waitForExit(15000);
            p.stop();
        }
        {
            AdbProcess p;
            std::string out;
            p.start({"-s", deviceSerial, "pull", remote, local},
                    [&](const std::string& l) { out += l; });
            p.waitForExit(30000);
            p.stop();
            bool ok = out.find("1 file pulled") != std::string::npos;
            { std::lock_guard<std::mutex> lock(m_stateMutex); m_actionMsg = ok ? "Screenshot saved: " + local
                             : "Screenshot failed: " + (out.empty() ? "no output" : out); }
        }
        {
            AdbProcess p;
            p.start({"-s", deviceSerial, "shell", "rm", "-f", remote},
                    [](const std::string&) {});
            p.waitForExit(5000);
            p.stop();
        }
        m_actionMsgEnd = static_cast<float>(ImGui::GetTime()) + 8.0f;
        m_actionRunning.store(false);
        scanCaptures(deviceSerial);
    }).detach();
}

void DeviceInfoPanel::takeScreenrecord(const std::string& deviceSerial, int seconds) {
    if (deviceSerial.empty() || m_actionRunning.load()) return;
    if (seconds < 3) seconds = 3;
    if (seconds > 180) seconds = 180;
    std::string local = capturesDirFor(deviceSerial) + "\\" + timestampName("screenrecord", "mp4");

    m_actionRunning.store(true);
    { std::lock_guard<std::mutex> lock(m_stateMutex); m_actionMsg = "Recording for " + std::to_string(seconds) + "s..."; }
    m_actionMsgEnd = static_cast<float>(ImGui::GetTime()) + 3.0f;

    std::thread([this, deviceSerial, local, seconds]() {
        const std::string remote = "/sdcard/logcater_rec.mp4";
        {
            AdbProcess p;
            p.start({"-s", deviceSerial, "shell", "screenrecord",
                     "--time-limit", std::to_string(seconds), remote},
                    [](const std::string&) {});
            p.waitForExit(static_cast<DWORD>(seconds * 1000 + 10000));
            p.stop();
        }
        {
            AdbProcess p;
            std::string out;
            p.start({"-s", deviceSerial, "pull", remote, local},
                    [&](const std::string& l) { out += l; });
            p.waitForExit(60000);
            p.stop();
            bool ok = out.find("1 file pulled") != std::string::npos;
            { std::lock_guard<std::mutex> lock(m_stateMutex); m_actionMsg = ok ? "Recording saved: " + local
                             : "Recording failed: " + (out.empty() ? "no output" : out); }
        }
        {
            AdbProcess p;
            p.start({"-s", deviceSerial, "shell", "rm", "-f", remote},
                    [](const std::string&) {});
            p.waitForExit(5000);
            p.stop();
        }
        m_actionMsgEnd = static_cast<float>(ImGui::GetTime()) + 8.0f;
        m_actionRunning.store(false);
        scanCaptures(deviceSerial);
    }).detach();
}

void DeviceInfoPanel::runDumpsys(const std::string& deviceSerial, const std::string& cmd) {
    if (deviceSerial.empty() || m_dumpsysLoading.load()) return;
    m_dumpsysLoading.store(true);
    m_dumpsysOutput = "Running: adb shell dumpsys " + cmd + " ...\n";

    std::thread([this, deviceSerial, cmd]() {
        std::string out;
        AdbProcess proc;
        proc.start({"-s", deviceSerial, "shell", "dumpsys", cmd},
                   [&](const std::string& l) { out += l + "\n"; });
        proc.waitForExit(15000);
        proc.stop();
        m_dumpsysOutput = out.empty() ? "(no output)" : out;
        m_dumpsysLoading.store(false);
    }).detach();
}

void DeviceInfoPanel::takeBugreport(const std::string& deviceSerial) {
    if (deviceSerial.empty() || m_actionRunning.load()) return;
    std::string dir = appDataBase() + "\\bugreports";
    CreateDirectoryA(dir.c_str(), nullptr);
    std::string local = dir + "\\" + timestampName("bugreport", "zip");

    m_actionRunning.store(true);
    { std::lock_guard<std::mutex> lock(m_stateMutex); m_actionMsg = "Collecting bugreport (may take a minute)..."; }
    m_actionMsgEnd = static_cast<float>(ImGui::GetTime()) + 3.0f;

    std::thread([this, deviceSerial, local]() {
        std::string out;
        AdbProcess proc;
        proc.start({"-s", deviceSerial, "bugreport", local},
                   [&](const std::string& l) { out += l; });
        proc.waitForExit(180000);
        proc.stop();
        bool ok = out.find("Bugreport") != std::string::npos ||
                  out.find("saved") != std::string::npos ||
                  out.empty(); // bugreport prints progress; empty output usually means file written
        { std::lock_guard<std::mutex> lock(m_stateMutex); m_actionMsg = ok ? "Bugreport saved: " + local
                         : "Bugreport failed: " + (out.empty() ? "no output" : out); }
        // Expand the ZIP so we can list key files (tombstones, ANR traces...)
        std::string extractDir = local.substr(0, local.size() - 4); // strip .zip
        {
            std::string ps = "powershell -NoProfile -Command \"Expand-Archive -Path '" +
                             local + "' -DestinationPath '" + extractDir + "' -Force\"";
            std::system(ps.c_str());
        }
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_bugreportDir = extractDir;
        }
        scanBugreport(deviceSerial);
        m_actionMsgEnd = static_cast<float>(ImGui::GetTime()) + 12.0f;
        m_actionRunning.store(false);
    }).detach();
}

void DeviceInfoPanel::takePerfetto(const std::string& deviceSerial, int seconds) {
    if (deviceSerial.empty() || m_actionRunning.load()) return;
    if (seconds < 3) seconds = 3;
    if (seconds > 120) seconds = 120;
    std::string local = capturesDirFor(deviceSerial) + "\\" + timestampName("trace", "perfetto-trace");

    m_actionRunning.store(true);
    { std::lock_guard<std::mutex> lock(m_stateMutex); m_actionMsg = "Capturing " + std::to_string(seconds) + "s Perfetto trace..."; }
    m_actionMsgEnd = static_cast<float>(ImGui::GetTime()) + 3.0f;

    std::thread([this, deviceSerial, local, seconds]() {
        const std::string remote = "/data/misc/perfetto-traces/logcater_trace.perfetto-trace";
        {
            AdbProcess p;
            p.start({"-s", deviceSerial, "shell", "perfetto",
                     "-o", remote, "-t", std::to_string(seconds) + "s",
                     "sched", "freq", "idle"},
                    [](const std::string&) {});
            p.waitForExit(static_cast<DWORD>(seconds * 1000 + 15000));
            p.stop();
        }
        {
            AdbProcess p;
            std::string out;
            p.start({"-s", deviceSerial, "pull", remote, local},
                    [&](const std::string& l) { out += l; });
            p.waitForExit(60000);
            p.stop();
            bool ok = out.find("1 file pulled") != std::string::npos;
            { std::lock_guard<std::mutex> lock(m_stateMutex); m_actionMsg = ok ? "Trace saved: " + local
                             : "Trace failed: " + (out.empty() ? "no output (may need shell permission)" : out); }
        }
        {
            AdbProcess p;
            p.start({"-s", deviceSerial, "shell", "rm", "-f", remote},
                    [](const std::string&) {});
            p.waitForExit(5000);
            p.stop();
        }
        m_actionMsgEnd = static_cast<float>(ImGui::GetTime()) + 8.0f;
        m_actionRunning.store(false);
        scanCaptures(deviceSerial);
    }).detach();
}

void DeviceInfoPanel::startFpsMonitor(const std::string& deviceSerial) {
    if (deviceSerial.empty() || m_fpsRunning.load()) return;
    std::string pkg(m_fpsPkg);
    // trim
    size_t b = pkg.find_first_not_of(" \t\r\n");
    size_t e = pkg.find_last_not_of(" \t\r\n");
    if (b == std::string::npos || e < b) return;
    pkg = pkg.substr(b, e - b + 1);
    if (pkg.empty()) return;

    m_fpsRunning.store(true);
    m_fpsStop.store(false);
    {
        std::lock_guard<std::mutex> lock(m_fpsMutex);
        m_fpsHistory.clear();
        m_fpsTotal = 0;
        m_fpsJanky = 0;
    }

    std::thread([this, deviceSerial, pkg]() {
        long long lastTotal = -1;
        auto lastTime = std::chrono::steady_clock::now();
        while (!m_fpsStop.load()) {
            std::string out;
            AdbProcess proc;
            proc.start({"-s", deviceSerial, "shell", "dumpsys", "gfxinfo", pkg},
                       [&](const std::string& l) { out += l + "\n"; });
            proc.waitForExit(8000);
            proc.stop();

            long long total = -1, janky = -1;
            std::istringstream iss(out);
            std::string line;
            while (std::getline(iss, line)) {
                size_t p = line.find("Total frames rendered:");
                if (p != std::string::npos) total = std::atoll(line.c_str() + p + 22);
                p = line.find("Janky frames:");
                if (p != std::string::npos) janky = std::atoll(line.c_str() + p + 13);
            }
            if (total >= 0) {
                auto now = std::chrono::steady_clock::now();
                std::lock_guard<std::mutex> lock(m_fpsMutex);
                if (lastTotal >= 0) {
                    double secs = std::chrono::duration<double>(now - lastTime).count();
                    if (secs > 0.5) {
                        float fps = static_cast<float>((total - lastTotal) / secs);
                        m_fpsHistory.push_back(fps);
                        if (m_fpsHistory.size() > 120) m_fpsHistory.erase(m_fpsHistory.begin());
                    }
                }
                m_fpsTotal = total;
                m_fpsJanky = janky;
                lastTotal = total;
                lastTime = now;
            }
            // Poll every 2s
            for (int i = 0; i < 20 && !m_fpsStop.load(); i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        m_fpsRunning.store(false);
    }).detach();
}

void DeviceInfoPanel::stopFpsMonitor() {
    m_fpsStop.store(true);
}

void DeviceInfoPanel::startMonkey(const std::string& deviceSerial) {
    if (deviceSerial.empty() || m_monkeyRunning.load()) return;
    std::string pkg(m_monkeyPkg);
    size_t b = pkg.find_first_not_of(" \t\r\n");
    size_t e = pkg.find_last_not_of(" \t\r\n");
    if (b == std::string::npos || e < b) return;
    pkg = pkg.substr(b, e - b + 1);
    if (pkg.empty()) return;

    m_monkeyRunning.store(true);
    m_monkeyStop.store(false);
    m_monkeyOutput = "Starting monkey on " + pkg + " (" + std::to_string(m_monkeyCount) + " events)...\n";

    std::thread([this, deviceSerial, pkg]() {
        AdbProcess proc;
        proc.start({"-s", deviceSerial, "shell", "monkey", "-p", pkg,
                    "--throttle", "100", std::to_string(m_monkeyCount)},
                   [this](const std::string& line) {
                       m_monkeyOutput += line + "\n";
                   });
        // Poll stop flag while waiting (monkey can take a while)
        while (!m_monkeyStop.load() && proc.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (m_monkeyStop.load()) {
            proc.stop();
            m_monkeyOutput += "\n[stopped by user]\n";
        } else {
            proc.waitForExit(600000);
            proc.stop();
            m_monkeyOutput += "\n[done]\n";
        }
        m_monkeyRunning.store(false);
    }).detach();
}

void DeviceInfoPanel::stopMonkey(const std::string& deviceSerial) {
    m_monkeyStop.store(true);
}

void DeviceInfoPanel::refreshForwards(const std::string& deviceSerial) {
    std::thread([this, deviceSerial]() {
        std::string out;
        AdbProcess proc;
        proc.start({"-s", deviceSerial, "forward", "--list"},
                   [&](const std::string& l) { out += l + "\n"; });
        proc.waitForExit(5000);
        proc.stop();
        { std::lock_guard<std::mutex> lock(m_stateMutex); m_forwardOutput = out.empty() ? "(no forward rules)" : out; }
    }).detach();
}

void DeviceInfoPanel::addForward(const std::string& deviceSerial, bool reverse) {
    const char* local = reverse ? m_revLocal : m_fwdLocal;
    const char* remote = reverse ? m_revRemote : m_fwdRemote;
    if (std::strlen(local) == 0 || std::strlen(remote) == 0) return;

    std::string l = "tcp:" + std::string(local);
    std::string r = "tcp:" + std::string(remote);
    std::thread([this, deviceSerial, l, r, reverse]() {
        AdbProcess proc;
        proc.start({"-s", deviceSerial, reverse ? "reverse" : "forward", l, r},
                   [](const std::string&) {});
        proc.waitForExit(5000);
        proc.stop();
        refreshForwards(deviceSerial);
    }).detach();
    if (!reverse) { m_fwdLocal[0] = '\0'; m_fwdRemote[0] = '\0'; }
    else          { m_revLocal[0] = '\0'; m_revRemote[0] = '\0'; }
}

void DeviceInfoPanel::removeAllForwards(const std::string& deviceSerial, bool reverse) {
    std::thread([this, deviceSerial, reverse]() {
        AdbProcess proc;
        proc.start({"-s", deviceSerial, reverse ? "reverse" : "forward", "--remove-all"},
                   [](const std::string&) {});
        proc.waitForExit(5000);
        proc.stop();
        refreshForwards(deviceSerial);
    }).detach();
}

// ─── Command helpers ───────────────────────────────────────────────

static std::string runAdb(const std::string& deviceSerial,
                          const std::vector<std::string>& args) {
    AdbProcess proc;
    std::string output;
    proc.start(args, [&](const std::string& line) {
        if (!output.empty()) output += "\n";
        output += line;
    });
    proc.waitForExit(5000);
    proc.stop();
    return output;
}

// ─── Parse helpers ─────────────────────────────────────────────────

void DeviceInfoPanel::parseBattery(const std::string& output, Info& info) {
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Format: "    key: value"
        std::regex re(R"(\s*(\S[^:]*):\s*(.*))");
        std::smatch m;
        if (std::regex_search(line, m, re)) {
            std::string key = m[1].str();
            std::string val = m[2].str();
            if (key == "level") info.batteryLevel = std::stoi(val);
            else if (key == "temperature") {
                int t = std::stoi(val);
                info.batteryTempC = t / 10.0f;
            }
            else if (key == "health") info.batteryHealth = val;
            else if (key == "status") info.batteryStatus = val;
        }
    }
}

void DeviceInfoPanel::parseDf(const std::string& output, Info& info) {
    // "adb shell df -h /data" output example:
    // Filesystem      Size  Used Avail Use% Mounted on
    // /dev/block/...   110G   45G   65G  41% /data
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find("/data") != std::string::npos &&
            line.find("Filesystem") == std::string::npos) {
            // Parse columns: Filesystem Size Used Avail Use% Mounted
            std::regex re(R"((\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\d+)%)");
            std::smatch m;
            if (std::regex_search(line, m, re)) {
                info.storageTotal  = m[2].str();
                info.storageUsed   = m[3].str();
                info.storageFree   = m[4].str();
                info.storagePercent = std::stoi(m[5].str());
            }
        }
    }
}

void DeviceInfoPanel::parseMeminfo(const std::string& output, Info& info) {
    // /proc/meminfo: "Key:  value kB"
    std::istringstream iss(output);
    std::string line;
    long totalKb = 0, availKb = 0;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::regex re(R"(^(\S+):\s+(\d+)\s)");
        std::smatch m;
        if (std::regex_search(line, m, re)) {
            std::string key = m[1].str();
            long val = std::stol(m[2].str());
            if (key == "MemTotal") totalKb = val;
            else if (key == "MemAvailable") availKb = val;
        }
    }

    // Format as human-readable
    auto fmt = [](long kb) -> std::string {
        if (kb >= 1048576) return std::to_string(kb / 1048576) + "." +
            std::to_string((kb % 1048576) * 10 / 1048576) + " GB";
        if (kb >= 1024) return std::to_string(kb / 1024) + " MB";
        return std::to_string(kb) + " KB";
    };
    info.memTotal = fmt(totalKb);
    info.memAvailable = fmt(availKb);
    if (totalKb > 0) {
        info.memPercent = static_cast<int>((totalKb - availKb) * 100 / totalKb);
    }
}

void DeviceInfoPanel::parseUptime(const std::string& output, Info& info) {
    // /proc/uptime: "12345.67 98765.43"
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::regex re(R"(^(\d+\.?\d*)\s)");
        std::smatch m;
        if (std::regex_search(line, m, re)) {
            double seconds = std::stod(m[1].str());
            int days = static_cast<int>(seconds) / 86400;
            int hours = (static_cast<int>(seconds) % 86400) / 3600;
            int mins = (static_cast<int>(seconds) % 3600) / 60;
            if (days > 0)
                info.uptime = std::to_string(days) + "d " + std::to_string(hours) + "h " + std::to_string(mins) + "m";
            else if (hours > 0)
                info.uptime = std::to_string(hours) + "h " + std::to_string(mins) + "m";
            else
                info.uptime = std::to_string(mins) + "m";
            break;
        }
    }
}

// ─── Refresh ───────────────────────────────────────────────────────

void DeviceInfoPanel::refresh(const std::string& deviceSerial) {
    if (m_loading.exchange(true)) return; // already running

    std::thread([this, deviceSerial]() {
        Info info;

        // System properties
        info.manufacturer = getProp(deviceSerial, "ro.product.manufacturer");
        info.model = getProp(deviceSerial, "ro.product.model");
        info.androidVersion = getProp(deviceSerial, "ro.build.version.release");
        info.sdkLevel = getProp(deviceSerial, "ro.build.version.sdk");
        info.buildFingerprint = getProp(deviceSerial, "ro.build.fingerprint");

        // Battery
        std::string batteryOut = runAdb(deviceSerial,
            {"-s", deviceSerial, "shell", "dumpsys", "battery"});
        parseBattery(batteryOut, info);

        // Storage
        std::string dfOut = runAdb(deviceSerial,
            {"-s", deviceSerial, "shell", "df", "-h", "/data"});
        parseDf(dfOut, info);

        // Memory
        std::string memOut = runAdb(deviceSerial,
            {"-s", deviceSerial, "shell", "cat", "/proc/meminfo"});
        parseMeminfo(memOut, info);

        // Uptime
        std::string uptimeOut = runAdb(deviceSerial,
            {"-s", deviceSerial, "shell", "cat", "/proc/uptime"});
        parseUptime(uptimeOut, info);

        info.valid = true;
        { std::lock_guard<std::mutex> lock(m_stateMutex); m_info = info; m_hasData = true; }
        m_loading.store(false);
    }).detach();
}

std::string DeviceInfoPanel::getProp(const std::string& deviceSerial,
                                      const std::string& key) {
    std::string out = runAdb(deviceSerial,
        {"-s", deviceSerial, "shell", "getprop", key});
    // Trim trailing whitespace/newlines
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    return out;
}

// ─── UI Render ─────────────────────────────────────────────────────

void DeviceInfoPanel::renderProgressBar(int percent, const char* label,
                                         float r, float g, float b) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(80);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(r, g, b, 0.7f));
    ImGui::ProgressBar(percent / 100.0f, ImVec2(-1, 0), "");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("%d%%", percent);
}

void DeviceInfoPanel::render(const std::string& deviceSerial) {
    // Auto-refresh
    float now = static_cast<float>(ImGui::GetTime());
    if (m_hasData && now - m_lastRefreshTime > AUTO_REFRESH_INTERVAL) {
        m_lastRefreshTime = now;
        refresh(deviceSerial);
    }

    // Refresh button
    if (ImGui::Button("Refresh")) {
        m_lastRefreshTime = now;
        refresh(deviceSerial);
    }
    ImGui::SameLine();
    if (m_loading.load())
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Loading...");
    else if (m_hasData)
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Last updated: %.0fs ago",
                           now - m_lastRefreshTime);
    else
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No data — click Refresh");

    // --- Screen capture / record tools ---
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "|");
    if (ImGui::Button("Screenshot")) {
        takeScreenshot(deviceSerial);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50);
    ImGui::InputInt("##recSec", &m_recordSeconds);
    if (m_recordSeconds < 3) m_recordSeconds = 3;
    if (m_recordSeconds > 180) m_recordSeconds = 180;
    ImGui::SameLine();
    if (ImGui::Button("Record")) {
        takeScreenrecord(deviceSerial, m_recordSeconds);
    }
    ImGui::SameLine();
    if (ImGui::Button("Bugreport")) {
        takeBugreport(deviceSerial);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Collect a full bugreport ZIP (contains tombstones, ANR traces, dumpsys...)");
    ImGui::SameLine();
    if (ImGui::Button("Perfetto")) {
        takePerfetto(deviceSerial, m_recordSeconds);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Capture a Perfetto trace (uses the seconds field, 3-120s)");
    ImGui::SameLine();
    std::string actionMsg;
    float actionEnd = 0.0f;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        actionMsg = m_actionMsg;
        actionEnd = m_actionMsgEnd;
    }
    if (m_actionRunning.load()) {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", actionMsg.c_str());
    } else if (!actionMsg.empty() && ImGui::GetTime() < actionEnd) {
        bool failed = actionMsg.find("failed") != std::string::npos;
        ImGui::TextColored(failed ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                                  : ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                           "%s", actionMsg.c_str());
    }

    ImGui::Separator();
    if (!m_hasData) return;

    Info d;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        d = m_info;
    }

    // ── System Info Card ──
    if (ImGui::CollapsingHeader("System", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("##sysinfo", 2, ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("");

            auto row = [](const char* label, const std::string& val) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(val.c_str());
            };
            row("Manufacturer", d.manufacturer);
            row("Model", d.model);
            row("Android", d.androidVersion + " (SDK " + d.sdkLevel + ")");
            row("Build", d.buildFingerprint);
            row("Uptime", d.uptime);
            ImGui::EndTable();
        }
    }

    // ── Battery Card ──
    if (ImGui::CollapsingHeader("Battery", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (d.batteryLevel >= 0) {
            float r = 0.4f, g = 0.9f, b = 0.4f;
            if (d.batteryLevel < 20) { r = 0.9f; g = 0.2f; b = 0.2f; }
            else if (d.batteryLevel < 50) { r = 1.0f; g = 0.7f; b = 0.0f; }
            renderProgressBar(d.batteryLevel, "Level", r, g, b);
        }
        if (d.batteryTempC >= 0) {
            float t = d.batteryTempC;
            ImGui::Text("Temperature: %.1f°C", t);
        }
        if (!d.batteryHealth.empty())
            ImGui::Text("Health: %s", d.batteryHealth.c_str());
        if (!d.batteryStatus.empty())
            ImGui::Text("Status: %s", d.batteryStatus.c_str());
    }

    // ── Storage Card ──
    if (ImGui::CollapsingHeader("Storage (/data)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (d.storagePercent >= 0) {
            float r = 0.4f, g = 0.9f, b = 0.4f;
            if (d.storagePercent > 90) { r = 0.9f; g = 0.2f; b = 0.2f; }
            else if (d.storagePercent > 70) { r = 1.0f; g = 0.7f; b = 0.0f; }
            renderProgressBar(d.storagePercent, "Used", r, g, b);
        }
        if (ImGui::BeginTable("##storage", 2, ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("");
            auto row = [](const char* label, const std::string& val) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(val.c_str());
            };
            row("Total", d.storageTotal);
            row("Used", d.storageUsed);
            row("Free", d.storageFree);
            ImGui::EndTable();
        }
    }

    // ── Memory Card ──
    if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (d.memPercent >= 0) {
            float r = 0.4f, g = 0.9f, b = 0.4f;
            if (d.memPercent > 90) { r = 0.9f; g = 0.2f; b = 0.2f; }
            else if (d.memPercent > 70) { r = 1.0f; g = 0.7f; b = 0.0f; }
            renderProgressBar(d.memPercent, "Used", r, g, b);
        }
        if (ImGui::BeginTable("##memory", 2, ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("");
            auto row = [](const char* label, const std::string& val) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(val.c_str());
            };
            row("Total", d.memTotal);
            row("Available", d.memAvailable);
            ImGui::EndTable();
        }
    }

    // ── Dumpsys Viewer ──
    if (ImGui::CollapsingHeader("Dumpsys", ImGuiTreeNodeFlags_DefaultOpen)) {
        static const char* cmds[] = {
            "meminfo", "cpuinfo", "gfxinfo", "battery", "window", "activity"
        };
        ImGui::SetNextItemWidth(140);
        if (ImGui::BeginCombo("##dumpsysCmd", cmds[m_dumpsysChoice])) {
            for (int i = 0; i < 6; i++) {
                if (ImGui::Selectable(cmds[i], i == m_dumpsysChoice)) {
                    m_dumpsysChoice = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Run")) {
            runDumpsys(deviceSerial, cmds[m_dumpsysChoice]);
        }
        ImGui::SameLine();
        if (m_dumpsysLoading.load()) {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Running...");
        } else if (!m_dumpsysOutput.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) {
                ImGui::SetClipboardText(m_dumpsysOutput.c_str());
            }
        }

        ImGui::BeginChild("DumpsysOut", ImVec2(0, 240), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        if (!m_dumpsysOutput.empty()) {
            ImGui::TextUnformatted(m_dumpsysOutput.c_str());
        }
        ImGui::EndChild();
    }

    // ── FPS Monitor ──
    if (ImGui::CollapsingHeader("FPS Monitor", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##fpsPkg", "package name (e.g. com.android.settings)",
                                 m_fpsPkg, sizeof(m_fpsPkg));
        ImGui::SameLine();
        if (!m_fpsRunning.load()) {
            if (ImGui::Button("Start")) {
                startFpsMonitor(deviceSerial);
            }
        } else {
            if (ImGui::Button("Stop")) {
                stopFpsMonitor();
            }
        }

        std::vector<float> history;
        long long total, janky;
        {
            std::lock_guard<std::mutex> lock(m_fpsMutex);
            history = m_fpsHistory;
            total = m_fpsTotal;
            janky = m_fpsJanky;
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "Total frames: %lld | Janky: %lld (%.1f%%)",
                           total, janky,
                           total > 0 ? (100.0 * janky / total) : 0.0);

        // Simple line chart of recent FPS
        float chartW = ImGui::GetContentRegionAvail().x;
        float chartH = 80.0f;
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + chartW, p0.y + chartH);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, p1, IM_COL32(20, 20, 24, 220));
        if (!history.empty()) {
            float maxFps = 120.0f;
            float minFps = 0.0f;
            float stepX = chartW / static_cast<float>(history.size());
            for (size_t i = 1; i < history.size(); i++) {
                float y0 = p1.y - (history[i - 1] - minFps) / (maxFps - minFps) * chartH;
                float y1 = p1.y - (history[i] - minFps) / (maxFps - minFps) * chartH;
                dl->AddLine(ImVec2(p0.x + (i - 1) * stepX, y0),
                            ImVec2(p0.x + i * stepX, y1),
                            IM_COL32(70, 200, 120, 255), 2.0f);
            }
        } else {
            dl->AddText(ImVec2(p0.x + 8, p0.y + chartH / 2 - 8),
                        IM_COL32(120, 120, 120, 200), "No samples yet");
        }
        ImGui::Dummy(ImVec2(chartW, chartH));
    }

    // ── Monkey Smoke Test ──
    if (ImGui::CollapsingHeader("Monkey Test", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##monkeyPkg", "package name",
                                 m_monkeyPkg, sizeof(m_monkeyPkg));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        ImGui::InputInt("##monkeyCount", &m_monkeyCount);
        if (m_monkeyCount < 1) m_monkeyCount = 1;
        ImGui::SameLine();
        if (!m_monkeyRunning.load()) {
            if (ImGui::Button("Start Monkey")) {
                startMonkey(deviceSerial);
            }
        } else {
            if (ImGui::Button("Stop Monkey")) {
                stopMonkey(deviceSerial);
            }
        }

        ImGui::BeginChild("MonkeyOut", ImVec2(0, 140), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        if (!m_monkeyOutput.empty()) {
            ImGui::TextUnformatted(m_monkeyOutput.c_str());
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }

    // ── Port Forwarding ──
    if (ImGui::CollapsingHeader("Port Forwarding", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Forward
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Forward (device listens):");
        ImGui::SetNextItemWidth(80);
        ImGui::InputTextWithHint("##fwdL", "local", m_fwdLocal, sizeof(m_fwdLocal));
        ImGui::SameLine();
        ImGui::TextUnformatted("->");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputTextWithHint("##fwdR", "remote", m_fwdRemote, sizeof(m_fwdRemote));
        ImGui::SameLine();
        if (ImGui::SmallButton("Add Forward")) addForward(deviceSerial, false);

        // Reverse
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Reverse (PC listens):");
        ImGui::SetNextItemWidth(80);
        ImGui::InputTextWithHint("##revL", "local", m_revLocal, sizeof(m_revLocal));
        ImGui::SameLine();
        ImGui::TextUnformatted("->");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputTextWithHint("##revR", "remote", m_revRemote, sizeof(m_revRemote));
        ImGui::SameLine();
        if (ImGui::SmallButton("Add Reverse")) addForward(deviceSerial, true);

        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh")) refreshForwards(deviceSerial);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove All")) removeAllForwards(deviceSerial, false);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reverse Remove All")) removeAllForwards(deviceSerial, true);

        ImGui::BeginChild("FwdOut", ImVec2(0, 100), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        std::string fwdOut;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            fwdOut = m_forwardOutput;
        }
        if (!fwdOut.empty()) {
            ImGui::TextUnformatted(fwdOut.c_str());
        }
        ImGui::EndChild();
    }

    // ── Captures (screenshots / recordings / traces) ──
    if (ImGui::CollapsingHeader("Captures", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::vector<CaptureEntry> caps;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            caps = m_captures;
        }
        if (ImGui::Button("Refresh List")) scanCaptures(deviceSerial);
        ImGui::SameLine();
        if (ImGui::Button("Open Folder")) {
            openInExplorer(capturesDirFor(deviceSerial) + "\\");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("ui.perfetto.dev")) {
            ShellExecuteA(nullptr, "open", "https://ui.perfetto.dev",
                          nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::SameLine();
        if (!caps.empty() && ImGui::Button("Clear All")) {
            clearAllCaptures(deviceSerial);
        }

        if (caps.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                               "No captures yet. Use Screenshot / Record / Perfetto above.");
        } else {
            ImGui::BeginChild("CaptureList", ImVec2(0, 160), true);
            for (int i = 0; i < (int)caps.size(); i++) {
                const auto& c = caps[i];
                ImGui::PushID(i);
                char sizeBuf[32];
                if (c.sizeBytes >= 1024 * 1024)
                    std::snprintf(sizeBuf, sizeof(sizeBuf), "%.1f MB", c.sizeBytes / 1048576.0);
                else if (c.sizeBytes >= 1024)
                    std::snprintf(sizeBuf, sizeof(sizeBuf), "%.1f KB", c.sizeBytes / 1024.0);
                else
                    std::snprintf(sizeBuf, sizeof(sizeBuf), "%lld B", (long long)c.sizeBytes);
                if (ImGui::Selectable((c.name + "  [" + sizeBuf + "]").c_str(),
                                      m_selectedCapture == i)) {
                    m_selectedCapture = i;
                    if (c.isImage) loadPreview(c.path);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Export")) exportCapture(deviceSerial, i);
                ImGui::SameLine();
                if (ImGui::SmallButton("Del")) deleteCapture(deviceSerial, i);
                if (!c.isImage) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Open")) {
                        ShellExecuteA(nullptr, "open", c.path.c_str(),
                                      nullptr, nullptr, SW_SHOWNORMAL);
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        // Image preview
        if (m_previewTexture) {
            float availW = ImGui::GetContentRegionAvail().x;
            float scale = availW / m_previewW;
            if (scale > 1.0f) scale = 1.0f;
            float scaleH = 420.0f / m_previewH;
            if (scale > scaleH) scale = scaleH;
            ImGui::Image((ImTextureID)(intptr_t)m_previewTexture,
                         ImVec2(m_previewW * scale, m_previewH * scale));
        }

        // Export-done confirm: delete the cache copy?
        int pendingIdx = -1;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            pendingIdx = m_exportPending;
            m_exportPending = -1;
        }
        if (pendingIdx >= 0) ImGui::OpenPopup("ExportDone");
        if (ImGui::BeginPopupModal("ExportDone", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Exported successfully. Delete the cache copy?");
            ImGui::Separator();
            if (ImGui::Button("Keep", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Delete", ImVec2(100, 0))) {
                deleteCapture(deviceSerial, pendingIdx);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // ── Bugreport files ──
    if (ImGui::CollapsingHeader("Bugreport Files", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::vector<BugreportFile> files;
        std::string bugDir;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            files = m_bugreportFiles;
            bugDir = m_bugreportDir;
        }
        if (!bugDir.empty() && ImGui::Button("Open Folder")) {
            openInExplorer(bugDir + "\\");
        }
        if (files.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                               "No bugreport collected yet. Use the Bugreport button above.");
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "%zu key file(s) (tombstones / ANR / traces / dropbox):",
                               files.size());
            ImGui::BeginChild("BugList", ImVec2(0, 140), true);
            for (const auto& f : files) {
                ImGui::PushID(f.name.c_str());
                char sizeBuf[32];
                std::snprintf(sizeBuf, sizeof(sizeBuf), "%lld B", (long long)f.sizeBytes);
                if (ImGui::Selectable((f.name + "  [" + sizeBuf + "]").c_str())) {
                    ShellExecuteA(nullptr, "open", f.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f.path.c_str());
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
    }
}
