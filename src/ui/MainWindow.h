#pragma once

#include "ui/DeviceSelector.h"
#include "ui/LogcatPanel.h"
#include "ui/DropboxPanel.h"
#include "ui/FileBrowserPanel.h"
#include "ui/AppInfoPanel.h"
#include "ui/DeviceInfoPanel.h"
#include "ui/HelpPanel.h"
#include "ui/WiFiPairingPanel.h"
#include "ui/ShellPanel.h"
#include "ui/SettingsPanel.h"
#include <string>
#include <map>
#include <memory>

class Application;

class MainWindow {
public:
    MainWindow();

    void render(Application& app);

private:
    DeviceSelector m_deviceSelector;
    DropboxPanel m_dropboxPanel;
    FileBrowserPanel m_fileBrowserPanel;
    AppInfoPanel m_appInfoPanel;
    DeviceInfoPanel m_deviceInfoPanel;
    HelpPanel m_helpPanel;
    WiFiPairingPanel m_wifiPanel;
    ShellPanel m_shellPanel;
    SettingsPanel m_settingsPanel;
    // Per-device logcat sessions: switching devices keeps each session alive.
    std::map<std::string, std::unique_ptr<LogcatPanel>> m_logcatPanels;
    int m_activeTab = 0;
    bool m_firstFrame = true;

    void openReleasePage();
    LogcatPanel& logcatPanelFor(const std::string& serial);
};
