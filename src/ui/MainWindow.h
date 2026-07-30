#pragma once

#include "ui/DeviceSelector.h"
#include "ui/LogcatPanel.h"
#include "ui/DropboxPanel.h"
#include "ui/FileBrowserPanel.h"
#include "ui/AppInfoPanel.h"
#include <string>

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
};
