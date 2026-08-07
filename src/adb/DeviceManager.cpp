#include "DeviceManager.h"
#include "core/AdbProcess.h"
#include <regex>
#include <sstream>
#include <set>
#include <thread>
#include <chrono>

DeviceManager::DeviceManager() = default;
DeviceManager::~DeviceManager() = default;

void DeviceManager::autoConnectMdns() {
    // Throttle: only attempt once per 10 seconds
    static auto lastTry = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (now - lastTry < std::chrono::seconds(10)) return;
    lastTry = now;

    // Discover WiFi ADB services (works for both classic "_adb._tcp" and
    // Android 11+ "_adb-tls-connect._tcp")
    AdbProcess mdns;
    std::vector<std::string> found;
    mdns.start({"mdns", "services"}, [&](const std::string& line) {
        if (line.find("_adb-tls-connect._tcp") == std::string::npos &&
            line.find("_adb._tcp") == std::string::npos) {
            return;
        }
        // Address token varies by adb version: "ip:port type name" or
        // "name type ip:port". Pick any token that looks like an ip:port.
        std::istringstream iss(line);
        std::string tok;
        while (iss >> tok) {
            if (tok.find(':') != std::string::npos &&
                tok.find('_') == std::string::npos &&
                tok.find('.') != std::string::npos) {
                found.push_back(tok);
                break;
            }
        }
    });
    mdns.waitForExit(3000);
    mdns.stop();
    if (found.empty()) return;

    // Skip services already present in the device list
    std::set<std::string> known;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& d : m_devices) {
            known.insert(d.serial);
        }
    }

    for (const auto& addr : found) {
        if (known.count(addr)) continue;
        AdbProcess conn;
        std::string out;
        conn.start({"connect", addr}, [&](const std::string& l) { out += l; });
        conn.waitForExit(5000);
        conn.stop();
    }
}

void DeviceManager::refreshAsync() {
    // Prevent concurrent refresh threads
    bool expected = false;
    if (!m_refreshing.compare_exchange_strong(expected, true)) return;

    std::thread([this]() {
        // Auto-connect WiFi devices discovered via mDNS before listing
        autoConnectMdns();

        std::vector<Device> newDevices;

        AdbProcess proc;
        proc.start({"devices", "-l"}, [&](const std::string& line) {
            if (line.empty() || line.find("List of devices") != std::string::npos)
                return;

            std::regex re(R"(^(\S+)\s+(\S+)(.*)$)");
            std::smatch match;
            if (std::regex_search(line, match, re)) {
                Device dev;
                dev.serial = match[1].str();
                dev.state = match[2].str();
                std::string attrs = match[3].str();
                std::regex modelRe(R"(model:(\S+))");
                std::smatch modelMatch;
                if (std::regex_search(attrs, modelMatch, modelRe)) {
                    dev.model = modelMatch[1].str();
                }
                newDevices.push_back(std::move(dev));
            }
        });

        // Wait for process to exit naturally (adb devices is fast, ~500ms)
        proc.waitForExit(3000);
        proc.stop();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            // Detect devices that appeared since the previous refresh.
            // The very first refresh only records the baseline (no popup).
            if (m_hasSeenDevices) {
                std::set<std::string> known(m_knownSerials.begin(), m_knownSerials.end());
                for (const auto& d : newDevices) {
                    if (!known.count(d.serial)) {
                        m_newDeviceSerial = d.serial;
                        m_newDeviceFlag = true;
                        break;
                    }
                }
            }
            m_hasSeenDevices = true;
            m_knownSerials.clear();
            for (const auto& d : newDevices) {
                m_knownSerials.push_back(d.serial);
            }
            m_devices = std::move(newDevices);
        }
        m_refreshDone.store(true);
        m_refreshing.store(false);
    }).detach();
}

bool DeviceManager::consumeNewDeviceFlag() {
    return m_newDeviceFlag.exchange(false);
}

std::string DeviceManager::lastNewDeviceSerial() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_newDeviceSerial;
}

std::vector<DeviceManager::Device> DeviceManager::devices() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_devices; // return copy — safe
}

std::optional<DeviceManager::Device> DeviceManager::selectedDevice() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& d : m_devices) {
        if (d.serial == m_selectedSerial) {
            return d; // return copy — safe
        }
    }
    return std::nullopt;
}

void DeviceManager::selectDevice(const std::string& serial) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_selectedSerial = serial;
}

bool DeviceManager::hasDevice() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_devices.empty();
}

bool DeviceManager::hasSelectedDevice() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& d : m_devices) {
        if (d.serial == m_selectedSerial) return true;
    }
    return false;
}

bool DeviceManager::refreshDone() const {
    return m_refreshDone.load();
}

void DeviceManager::clearRefreshFlag() {
    m_refreshDone.store(false);
}

void DeviceManager::refreshProcessMap(const std::string& deviceSerial) {
    // Prevent duplicate calls
    bool expected = false;
    if (!m_processMapPending.compare_exchange_strong(expected, true)) return;

    // Run in background to avoid blocking UI
    std::thread([this, deviceSerial]() {
        AdbProcess proc;
        std::string output;

        proc.start({"-s", deviceSerial, "shell", "ps", "-A", "-o", "PID,NAME"},
            [&](const std::string& line) {
                output += line + "\n";
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        proc.stop();

        if (output.empty()) { m_processMapPending.store(false); return; }

        std::unordered_map<int, std::string> newMap;
        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
            // Trim \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.find("PID") != std::string::npos) continue;

            // Find first digit (PID start) and parse
            const char* p = line.c_str();
            while (*p && std::isspace(static_cast<unsigned char>(*p))) p++;
            if (!*p || !std::isdigit(static_cast<unsigned char>(*p))) continue;

            char* end;
            long pid = std::strtol(p, &end, 10);
            if (pid <= 0 || pid > INT_MAX) continue;

            // Skip whitespace to get name
            p = end;
            while (*p && std::isspace(static_cast<unsigned char>(*p))) p++;
            std::string name(p);
            // Trim trailing whitespace from name
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();

            if (!name.empty()) {
                newMap[static_cast<int>(pid)] = name;
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_processMutex);
            m_processMap = std::move(newMap);
        }
        m_processMapPending.store(false);
    }).detach();
}

std::string DeviceManager::processName(int pid) const {
    std::lock_guard<std::mutex> lock(m_processMutex);
    auto it = m_processMap.find(pid);
    if (it != m_processMap.end()) return it->second;
    return "";
}

size_t DeviceManager::processMapSize() const {
    std::lock_guard<std::mutex> lock(m_processMutex);
    return m_processMap.size();
}

std::vector<std::string> DeviceManager::allProcessNames() const {
    std::lock_guard<std::mutex> lock(m_processMutex);
    std::set<std::string> unique;
    for (const auto& kv : m_processMap) {
        if (!kv.second.empty()) unique.insert(kv.second);
    }
    return std::vector<std::string>(unique.begin(), unique.end());
}

bool DeviceManager::processMapReady() const {
    return !m_processMapPending.load() && processMapSize() > 0;
}

void DeviceManager::setProcessMapPending() {
    m_processMapPending.store(false);
}

void DeviceManager::refreshInstalledPackages(const std::string& deviceSerial) {
    bool expected = false;
    if (!m_packagesLoading.compare_exchange_strong(expected, true)) return;

    std::thread([this, deviceSerial]() {
        AdbProcess proc;
        std::string output;

        // Get third-party packages (fast, ~1s)
        proc.start({"-s", deviceSerial, "shell", "pm", "list", "packages", "-3"},
            [&](const std::string& line) {
                output += line + "\n";
            });
        proc.waitForExit(5000);
        proc.stop();

        std::vector<std::string> pkgs;
        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("package:", 0) == 0) {
                pkgs.push_back(line.substr(8));
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_packagesMutex);
            m_installedPackages = std::move(pkgs);
        }
        m_packagesLoading.store(false);
    }).detach();
}

std::vector<std::string> DeviceManager::installedPackages() const {
    std::lock_guard<std::mutex> lock(m_packagesMutex);
    return m_installedPackages;
}

bool DeviceManager::installedPackagesReady() const {
    std::lock_guard<std::mutex> lock(m_packagesMutex);
    return !m_packagesLoading.load() && !m_installedPackages.empty();
}
