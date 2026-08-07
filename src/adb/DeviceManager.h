#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <optional>
#include <unordered_map>

class AdbProcess;

class DeviceManager {
public:
    struct Device {
        std::string serial;
        std::string model;
        std::string state; // "device", "offline", "unauthorized"
    };

    DeviceManager();
    ~DeviceManager();

    /// Run "adb devices -l" asynchronously. Results available next frame.
    void refreshAsync();

    /// Copy of current device list (thread-safe).
    std::vector<Device> devices() const;

    /// Copy of selected device (thread-safe).
    std::optional<Device> selectedDevice() const;

    void selectDevice(const std::string& serial);
    bool hasDevice() const;
    bool hasSelectedDevice() const;

    /// True when a device not seen before appeared in the latest refresh.
    /// The flag is cleared once read.
    bool consumeNewDeviceFlag();
    std::string lastNewDeviceSerial() const;

    /// Set to true when async refresh completes.
    bool refreshDone() const;
    void clearRefreshFlag();

    /// Build PID→process-name map from "ps -A".
    void refreshProcessMap(const std::string& deviceSerial);

    /// Look up process name by PID.
    std::string processName(int pid) const;

    /// Get all unique process names (from ps -A).
    std::vector<std::string> allProcessNames() const;

    /// Number of entries in the process map.
    size_t processMapSize() const;
    bool processMapReady() const;
    void setProcessMapPending();

    /// Load installed package names via "pm list packages -3".
    void refreshInstalledPackages(const std::string& deviceSerial);

    /// Get all installed package names.
    std::vector<std::string> installedPackages() const;
    bool installedPackagesReady() const;

private:
    std::vector<Device> m_devices;
    std::string m_selectedSerial;
    std::vector<std::string> m_knownSerials;
    std::string m_newDeviceSerial;
    std::atomic<bool> m_newDeviceFlag{false};
    mutable std::mutex m_mutex;
    std::atomic<bool> m_refreshDone{false};
    std::atomic<bool> m_refreshing{false};

    /// Discover WiFi ADB devices via "adb mdns services" and connect to any
    /// not already in the device list. Throttled to once per 10 seconds.
    void autoConnectMdns();

    std::unordered_map<int, std::string> m_processMap;
    mutable std::mutex m_processMutex;
    std::atomic<bool> m_processMapPending{false};

    std::vector<std::string> m_installedPackages;
    mutable std::mutex m_packagesMutex;
    std::atomic<bool> m_packagesLoading{false};
};
