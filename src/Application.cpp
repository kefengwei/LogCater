#include "Application.h"
#include "adb/DeviceManager.h"
#include "ui/MainWindow.h"
#include "core/Settings.h"
#include <memory>

Application::Application() = default;
Application::~Application() = default;

bool Application::init(Settings& settings) {
    m_settings = &settings;
    m_deviceManager = std::make_unique<DeviceManager>();
    m_mainWindow = std::make_unique<MainWindow>();
    return true;
}

void Application::render() {
    if (m_mainWindow) {
        m_mainWindow->render(*this);
    }
}

void Application::postFrame() {
    // Debounced settings save, etc.
}

void Application::shutdown() {
    // MainWindow will handle stopping logcat etc.
}

void Application::onDeviceSelected(const std::string& serial) {
    // Handled by MainWindow callback chain
}
