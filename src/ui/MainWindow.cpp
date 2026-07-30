#include "MainWindow.h"
#include "Application.h"
#include "adb/DeviceManager.h"
#include "core/Settings.h"
#include "core/UpdateChecker.h"
#include "imgui.h"
#include <Windows.h>
#include <shellapi.h>

#ifndef LOGCATER_VERSION
#define LOGCATER_VERSION "0.0.0"
#endif
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)

MainWindow::MainWindow() {
    m_deviceSelector.setOnDeviceChanged([this](const std::string& serial) {
        m_logcatPanel.stop();
        if (!serial.empty()) {
            m_logcatPanel.start(serial);
        }
    });
}

void MainWindow::triggerUpdateCheck() {
    if (m_updateCheckDone.load()) return;
    m_updateCheckDone.store(true);

    UpdateChecker::check(STRINGIFY(LOGCATER_VERSION),
        [this](const UpdateChecker::UpdateInfo& info) {
            m_updateInfo = info;
        });
}

void MainWindow::openReleasePage() {
    std::string url = m_updateInfo.downloadUrl;
    if (url.empty()) {
        url = "https://github.com/kefengwei/LogCater/releases";
    }
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOW);
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
        triggerUpdateCheck();
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
        ImGui::TextUnformatted("Device:");
        ImGui::SameLine();
        m_deviceSelector.render(dm, 220.0f);

        // Update notification
        if (m_updateInfo.hasUpdate) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "|");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                               "New version: %s",
                               m_updateInfo.latestVersion.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Open Release Page")) {
                openReleasePage();
            }
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 130);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                           "LogCater v" STRINGIFY(LOGCATER_VERSION));

        ImGui::SameLine();
        if (ImGui::SmallButton("?")) {
            m_updateCheckDone.store(false);
            triggerUpdateCheck();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Check for updates");

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
                m_logcatPanel.setDeviceManager(&dm);
                if (!m_logcatPanel.isRunning()) {
                    m_logcatPanel.start(selected->serial);
                }
                if (!dm.processMapReady()) {
                    dm.refreshProcessMap(selected->serial);
                }
                m_logcatPanel.render(settings);
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

        if (ImGui::BeginTabItem("Help")) {
            m_activeTab = 4;
            m_helpPanel.render();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // --- Status bar ---
    ImGui::Separator();
    auto selected = dm.selectedDevice();
    if (selected.has_value()) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Device: %s (%s)",
                           selected->serial.c_str(),
                           selected->model.empty() ? selected->state.c_str() : selected->model.c_str());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "No device connected");
    }

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "|");
    ImGui::SameLine();

    if (m_logcatPanel.isRunning()) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Logcat: Running");
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Logcat: Stopped");
    }

    if (m_updateInfo.hasUpdate) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "|");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                           "New version: %s", m_updateInfo.latestVersion.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Open Release Page")) {
            openReleasePage();
        }
    }

    ImGui::End();

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
