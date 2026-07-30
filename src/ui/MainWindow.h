#pragma once

#include "ui/DeviceSelector.h"
#include "ui/LogcatPanel.h"
#include "ui/DropboxPanel.h"
#include "ui/FileBrowserPanel.h"
#include "ui/AppInfoPanel.h"
#include "core/UpdateChecker.h"
#include <string>
#include <atomic>
#include <mutex>

class Application;

class MainWindow {
public:
    MainWindow();

    void render(Application& app);

private:
    DeviceSelector m_deviceSelector;
    LogcatPanel m_logcatPanel;
    DropboxPanel m_dropboxPanel;
    FileBrowserPanel m_fileBrowserPanel;
    AppInfoPanel m_appInfoPanel;
    int m_activeTab = 0;
    bool m_firstFrame = true;

    // Update checker
    UpdateChecker::UpdateInfo m_updateInfo;
    std::atomic<bool> m_updateCheckDone{false};
    void triggerUpdateCheck();
    void startUpdateDownload();

    // Download progress state
    bool m_updateDownloading = false;
    std::string m_updateStatus;
    float m_updateProgress = 0.0f;
    std::string m_updateError;
    std::mutex m_updateMutex;
};
