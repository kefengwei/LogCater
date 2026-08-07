#include "MainWindow.h"
#include "Application.h"
#include "adb/DeviceManager.h"
#include "core/Settings.h"
#include "imgui.h"
#include <Windows.h>
#include <shellapi.h>

extern "C" void LogCaterRequestTheme(int theme);

#ifndef LOGCATER_VERSION
#define LOGCATER_VERSION "0.0.0"
#endif
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)

MainWindow::MainWindow() {
    // Logcat sessions are per-device (see logcatPanelFor); nothing to do on switch.
    m_deviceSelector.setOnDeviceChanged([](const std::string&) {});
}

LogcatPanel& MainWindow::logcatPanelFor(const std::string& serial) {
    auto it = m_logcatPanels.find(serial);
    if (it == m_logcatPanels.end()) {
        auto panel = std::make_unique<LogcatPanel>();
        panel->start(serial);
        it = m_logcatPanels.emplace(serial, std::move(panel)).first;
    }
    return *it->second;
}

void MainWindow::openReleasePage() {
    ShellExecuteA(nullptr, "open",
        "https://github.com/kefengwei/LogCater/releases",
        nullptr, nullptr, SW_SHOW);
}

void MainWindow::render(Application& app) {
    auto& dm = app.deviceManager();
    auto& settings = app.settings();

    if (m_firstFrame) {
        m_firstFrame = false;
        if (settings.mainWindowPosX >= 0 && settings.mainWindowPosY >= 0) {
            ImGui::SetWindowPos(ImVec2(settings.mainWindowPosX, settings.mainWindowPosY));
        }
        if (settings.mainWindowWidth > 0 && settings.mainWindowHeight > 0) {
            ImGui::SetWindowSize(ImVec2(settings.mainWindowWidth, settings.mainWindowHeight));
        }
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoNavFocus |
                                   ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGui::Begin("LogCater", nullptr, windowFlags);
    ImGui::PopStyleVar(2);

    // --- Menu bar ---
    if (ImGui::BeginMenuBar()) {
        ImGui::TextColored(ImVec4(0.57f, 0.60f, 0.64f, 1.0f), "Device");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300.0f);
        m_deviceSelector.render(dm, 300.0f);

        ImGui::Separator();
        ImGui::SameLine();

        ImGui::TextColored(ImVec4(0.57f, 0.60f, 0.64f, 1.0f), "Quick Links");
        ImGui::SameLine();

        if (ImGui::SmallButton("WiFi")) {
            m_wifiPanel.open();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("WiFi ADB pairing (Android 11+ wireless debugging)");

        ImGui::SameLine();
        if (ImGui::SmallButton("Check for Updates")) {
            openReleasePage();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Open GitHub Releases page");

        ImGui::SameLine();
        if (ImGui::SmallButton(settings.uiTheme == 1 ? "Theme: Light" : "Theme: Dark")) {
            settings.uiTheme = (settings.uiTheme == 1) ? 0 : 1;
            settings.save(Settings::defaultPath());
            LogCaterRequestTheme(settings.uiTheme);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Switch dark/light theme");

        ImGui::SameLine(ImGui::GetWindowWidth() - 190);
        ImGui::TextColored(ImVec4(0.57f, 0.60f, 0.64f, 1.0f),
                           "LogCater v" STRINGIFY(LOGCATER_VERSION));

        ImGui::EndMenuBar();
    }

    // --- Tab bar ---
    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Logcat")) {
            m_activeTab = 0;
            auto selected = dm.selectedDevice();
            if (!selected.has_value()) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                                   "Please select a device to start logcat.");
            } else {
                LogcatPanel& panel = logcatPanelFor(selected->serial);
                panel.setDeviceManager(&dm);
                if (!dm.processMapReady()) {
                    dm.refreshProcessMap(selected->serial);
                }
                panel.render(settings);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Dropbox")) {
            m_activeTab = 1;
            auto selected = dm.selectedDevice();
            if (!selected.has_value()) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                                   "Please select a device to view dropbox entries.");
            } else {
                m_dropboxPanel.render(settings, selected->serial);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Files")) {
            m_activeTab = 2;
            auto selected = dm.selectedDevice();
            if (!selected.has_value()) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                                   "Please select a device to browse files.");
            } else {
                if (!dm.processMapReady()) {
                    dm.refreshProcessMap(selected->serial);
                }
                if (!dm.installedPackagesReady()) {
                    dm.refreshInstalledPackages(selected->serial);
                }
                m_fileBrowserPanel.render(selected->serial, dm, settings);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Apps")) {
            m_activeTab = 3;
            auto selected = dm.selectedDevice();
            if (!selected.has_value()) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                                   "Please select a device to view apps.");
            } else {
                m_appInfoPanel.render(selected->serial);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Device")) {
            m_activeTab = 4;
            auto selected = dm.selectedDevice();
            if (!selected.has_value()) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                                   "Please select a device to view info.");
            } else {
                m_deviceInfoPanel.render(selected->serial);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Shell")) {
            m_activeTab = 5;
            auto selected = dm.selectedDevice();
            if (!selected.has_value()) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                                   "Please select a device to open a shell.");
            } else {
                m_shellPanel.render(selected->serial);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Help")) {
            m_activeTab = 6;
            m_helpPanel.render();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Settings")) {
            m_activeTab = 7;
            m_settingsPanel.render(settings);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // --- Status bar ---
    ImGui::Separator();
    auto selected = dm.selectedDevice();
    if (selected.has_value()) {
        ImGui::TextColored(ImVec4(0.24f, 0.86f, 0.52f, 1.0f), "●");
        ImGui::SameLine();
        ImGui::TextUnformatted("Device:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.91f, 0.93f, 0.94f, 1.0f), "%s",
                           selected->serial.c_str());
        if (!selected->model.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.60f, 0.63f, 0.67f, 1.0f), "(%s)",
                               selected->model.c_str());
        }
        if (!selected->state.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.45f, 0.70f, 0.85f, 1.0f), "[%s]",
                               selected->state.c_str());
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.38f, 1.0f), "●");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "No device connected");
    }

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.57f, 0.60f, 0.64f, 1.0f), "|");
    ImGui::SameLine();

    bool logcatRunning = false;
    if (auto sel = dm.selectedDevice()) {
        auto it = m_logcatPanels.find(sel->serial);
        logcatRunning = it != m_logcatPanels.end() && it->second->isRunning();
    }
    if (logcatRunning) {
        ImGui::TextColored(ImVec4(0.24f, 0.86f, 0.52f, 1.0f), "● Logcat: Running");
    } else {
        ImGui::TextColored(ImVec4(0.57f, 0.60f, 0.64f, 1.0f), "○ Logcat: Stopped");
    }

    ImGui::End();

    // Render WiFi pairing popup (if open); triggers device refresh on success
    m_wifiPanel.render(dm);
    if (m_wifiPanel.justConnected()) {
        m_wifiPanel.clearConnectedFlag();
        dm.refreshAsync();
    }

    // Save window position/size
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    if (pos.x > 0 && pos.y > 0) {
        settings.mainWindowPosX = pos.x;
        settings.mainWindowPosY = pos.y;
    }
    if (size.x > 0 && size.y > 0) {
        settings.mainWindowWidth = size.x;
        settings.mainWindowHeight = size.y;
    }
}
