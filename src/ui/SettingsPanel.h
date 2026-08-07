#pragma once

#include <string>

class Settings;

/// App settings panel — theme, log buffer size, highlight keywords, ADB path.
class SettingsPanel {
public:
    void render(Settings& settings);
};
