#pragma once

#include <string>
#include <functional>

class DeviceManager;

class DeviceSelector {
public:
    void render(DeviceManager& deviceManager, float width = 250.0f);

    void setOnDeviceChanged(std::function<void(const std::string&)> callback) {
        m_onChanged = std::move(callback);
    }

private:
    std::function<void(const std::string&)> m_onChanged;
    float m_lastRefreshTime = 0.0f;
    static constexpr float REFRESH_INTERVAL = 3.0f;
};
