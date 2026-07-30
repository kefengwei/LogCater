#include "DeviceSelector.h"
#include "adb/DeviceManager.h"
#include "imgui.h"

void DeviceSelector::render(DeviceManager& dm, float width) {
    float now = static_cast<float>(ImGui::GetTime());

    // Trigger async refresh periodically; check completion each frame
    if (now - m_lastRefreshTime > REFRESH_INTERVAL) {
        dm.refreshAsync();
        m_lastRefreshTime = now;
    }

    ImGui::SetNextItemWidth(width);
    auto devices = dm.devices(); // copy — thread-safe

    if (devices.empty()) {
        ImGui::TextUnformatted("No devices found");
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            dm.refreshAsync();
            m_lastRefreshTime = now;
        }
        return;
    }

    auto selected = dm.selectedDevice(); // copy — thread-safe
    std::string preview = selected.has_value()
        ? selected->serial + (selected->model.empty() ? "" : " (" + selected->model + ")")
        : "Select device...";

    if (ImGui::BeginCombo("##device", preview.c_str())) {
        for (const auto& dev : devices) {
            bool isSelected = selected.has_value() && selected->serial == dev.serial;
            std::string label = dev.serial;
            if (!dev.model.empty()) {
                label += " (" + dev.model + ")";
            }
            label += " [" + dev.state + "]";

            if (ImGui::Selectable(label.c_str(), isSelected)) {
                dm.selectDevice(dev.serial);
                if (m_onChanged) {
                    m_onChanged(dev.serial);
                }
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        dm.refreshAsync();
        m_lastRefreshTime = now;
    }
}
