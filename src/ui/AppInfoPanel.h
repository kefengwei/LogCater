#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include "imgui.h"

class Settings;

struct AppInfo {
    std::string packageName;
    std::string appName;        // human-readable label from dumpsys
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
    std::string m_contextPkg;

    // APK install (drag-and-drop)
    std::atomic<bool> m_installing{false};
    std::atomic<bool> m_pendingRefresh{false};
    struct InstallStatus {
        std::string currentFile;
        std::string message;
        int done = 0;
        int total = 0;
        bool lastFailed = false;
    };
    InstallStatus m_installStatus;
    std::mutex m_installMutex;
    int m_lastNotifiedDone = 0;   // completed count already shown in a toast

    // Toast banner
    std::string m_toastMsg;
    ImVec4 m_toastColor = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
    float m_toastEndTime = 0.0f;

    void showToast(const std::string& msg, const ImVec4& color);
    void renderToast();

    void refreshAppList(const std::string& deviceSerial);
    void installApks(const std::string& deviceSerial, std::vector<std::string> paths);
    void runPkgCommand(const std::string& deviceSerial, std::vector<std::string> args,
                       const std::string& successMsg);
};
