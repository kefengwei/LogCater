#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <vector>

/// Device information dashboard — battery, storage, memory, system info at a glance.
/// Runs multiple ADB commands on a background thread; results displayed as cards.
class DeviceInfoPanel {
public:
    DeviceInfoPanel();
    ~DeviceInfoPanel();

    /// Render the device info dashboard for the given device serial.
    void render(const std::string& deviceSerial);

private:
    // Cached device info (populated by background refresh)
    struct Info {
        // System
        std::string manufacturer;
        std::string model;
        std::string androidVersion;
        std::string sdkLevel;
        std::string buildFingerprint;

        // Battery
        int    batteryLevel = -1;       // 0-100, -1 = unknown
        float  batteryTempC = -1.0f;    // celsius
        std::string batteryHealth;
        std::string batteryStatus;

        // Storage (/data partition)
        std::string storageTotal;
        std::string storageUsed;
        std::string storageFree;
        int    storagePercent = -1;

        // Memory
        std::string memTotal;
        std::string memAvailable;
        int    memPercent = -1;

        // Uptime
        std::string uptime;

        bool valid = false;
    };

    Info m_info;
    mutable std::mutex m_stateMutex;   // guards m_info, m_actionMsg, m_forwardOutput
    std::string m_actionMsg;
    std::atomic<bool> m_loading{false};
    std::atomic<bool> m_actionRunning{false};
    float m_actionMsgEnd = 0.0f;
    std::atomic<bool> m_hasData{false};
    float m_lastRefreshTime = 0.0f;
    static constexpr float AUTO_REFRESH_INTERVAL = 10.0f;
    int m_recordSeconds = 30;

    // Dumpsys viewer
    int m_dumpsysChoice = 0;
    std::string m_dumpsysOutput;
    std::atomic<bool> m_dumpsysLoading{false};

    // FPS monitor (gfxinfo polling)
    char m_fpsPkg[128] = {};
    std::atomic<bool> m_fpsRunning{false};
    std::atomic<bool> m_fpsStop{false};
    std::mutex m_fpsMutex;
    std::vector<float> m_fpsHistory;   // recent FPS samples (oldest first)
    long long m_fpsTotal = 0;
    long long m_fpsJanky = 0;

    void startFpsMonitor(const std::string& deviceSerial);
    void stopFpsMonitor();

    // Monkey smoke test
    char m_monkeyPkg[128] = {};
    int m_monkeyCount = 1000;
    std::string m_monkeyOutput;
    std::atomic<bool> m_monkeyRunning{false};
    std::atomic<bool> m_monkeyStop{false};
    std::atomic<bool> m_monkeyDirty{false};
    std::string m_renderMonkeyOutput;

    void startMonkey(const std::string& deviceSerial);
    void stopMonkey(const std::string& deviceSerial);

    // Port forwarding
    char m_fwdLocal[16] = {};
    char m_fwdRemote[16] = {};
    char m_revLocal[16] = {};
    char m_revRemote[16] = {};
    std::string m_forwardOutput;

    void refreshForwards(const std::string& deviceSerial);
    void addForward(const std::string& deviceSerial, bool reverse);
    void removeAllForwards(const std::string& deviceSerial, bool reverse);

    // ── Captures (screenshots / recordings / traces) ──
    struct CaptureEntry {
        std::string name;
        std::string path;
        int64_t sizeBytes = 0;
        bool isImage = false;
    };
    std::vector<CaptureEntry> m_captures;
    int m_selectedCapture = -1;
    unsigned int m_previewTexture = 0;   // GL texture for the selected PNG
    int m_previewW = 0, m_previewH = 0;
    int m_exportPending = -1;            // capture index awaiting "delete cache?" confirm
    std::atomic<bool> m_autoSelectCapture{false}; // select newest after capture

    void scanCaptures(const std::string& deviceSerial);
    void exportCapture(const std::string& deviceSerial, int index);
    void deleteCapture(const std::string& deviceSerial, int index);
    void clearAllCaptures(const std::string& deviceSerial);
    void loadPreview(const std::string& path);
    void destroyPreview();

    // ── Bugreport list ──
    struct BugreportFile {
        std::string name;
        std::string path;
        int64_t sizeBytes = 0;
    };
    std::vector<BugreportFile> m_bugreportFiles;
    std::string m_bugreportDir;

    void scanBugreport(const std::string& deviceSerial);
    void openInExplorer(const std::string& path);

    static std::string appDataBase();
    static std::string capturesDirFor(const std::string& serial);

    void refresh(const std::string& deviceSerial);
    void takeScreenshot(const std::string& deviceSerial);
    void takeScreenrecord(const std::string& deviceSerial, int seconds);
    void takeBugreport(const std::string& deviceSerial);
    void takePerfetto(const std::string& deviceSerial, int seconds);
    void runDumpsys(const std::string& deviceSerial, const std::string& cmd);

    /// Parse helpers
    static std::string getProp(const std::string& deviceSerial, const std::string& key);
    static void parseBattery(const std::string& output, Info& info);
    static void parseDf(const std::string& output, Info& info);
    static void parseMeminfo(const std::string& output, Info& info);
    static void parseUptime(const std::string& output, Info& info);

    /// Render helpers
    void renderCard(const char* title, const char* icon);
    void renderProgressBar(int percent, const char* label, float r, float g, float b);
};
