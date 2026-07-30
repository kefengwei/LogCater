#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>

class Settings;

struct AppInfo {
    std::string packageName;
    std::string versionName;
    int versionCode = 0;
    int targetSdk = 0;
    bool detailsLoaded = false;
};

class AppInfoPanel {
public:
    AppInfoPanel();
    ~AppInfoPanel();

    void render(const std::string& deviceSerial);

    /// App source filter: 0=third-party, 1=system, 2=all
    int m_appSource = 0;

private:
    std::vector<AppInfo> m_apps;
    std::mutex m_mutex;
    std::atomic<bool> m_loading{false};
    std::atomic<int> m_loadedCount{0};
    std::atomic<int> m_totalCount{0};
    std::string m_textFilter;
    std::string m_lastDeviceSerial;

    void refreshAppList(const std::string& deviceSerial);
};
