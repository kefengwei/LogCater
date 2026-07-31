#pragma once

#include <string>
#include <atomic>

/// Device information dashboard — battery, storage, memory, system info at a glance.
/// Runs multiple ADB commands on a background thread; results displayed as cards.
class DeviceInfoPanel {
public:
    DeviceInfoPanel() = default;

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
    std::atomic<bool> m_loading{false};
    bool m_hasData = false;
    float m_lastRefreshTime = 0.0f;
    static constexpr float AUTO_REFRESH_INTERVAL = 10.0f;

    void refresh(const std::string& deviceSerial);

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
