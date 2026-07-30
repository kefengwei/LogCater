#pragma once

#include <string>
#include <memory>

// Forward declarations
class DeviceManager;
class MainWindow;
class Settings;

class Application {
public:
    Application();
    ~Application();

    bool init(Settings& settings);
    void render();
    void postFrame();
    void shutdown();

    DeviceManager& deviceManager() { return *m_deviceManager; }
    Settings& settings() { return *m_settings; }

    void onDeviceSelected(const std::string& serial);

private:
    std::unique_ptr<DeviceManager> m_deviceManager;
    std::unique_ptr<MainWindow> m_mainWindow;
    Settings* m_settings = nullptr;
};
