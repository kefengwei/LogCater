#include "SettingsPanel.h"
#include "core/Settings.h"
#include "imgui.h"
#include <cstring>

extern "C" void LogCaterRequestTheme(int theme);

void SettingsPanel::render(Settings& settings) {
    bool changed = false;

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Appearance");
    ImGui::Indent();
    if (ImGui::RadioButton("Dark theme", settings.uiTheme == 0)) {
        if (settings.uiTheme != 0) { settings.uiTheme = 0; changed = true; }
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Light theme", settings.uiTheme == 1)) {
        if (settings.uiTheme != 1) { settings.uiTheme = 1; changed = true; }
    }
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Logcat");
    ImGui::Indent();
    int bufSize = settings.logcatBufferSize;
    ImGui::SetNextItemWidth(260);
    if (ImGui::SliderInt("Log buffer size", &bufSize, 10000, 500000)) {
        settings.logcatBufferSize = bufSize;
        changed = true;
    }
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                       "Takes effect when a new logcat session starts.");
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Highlight keywords");
    ImGui::Indent();
    static char kwBuf[512] = {};
    if (kwBuf[0] == '\0' && !settings.highlightKeywords.empty()) {
        std::strncpy(kwBuf, settings.highlightKeywords.c_str(), sizeof(kwBuf) - 1);
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint(
            "##highlightKw",
            "Comma-separated keywords (e.g. Fatal, Crash, Error)",
            kwBuf, sizeof(kwBuf))) {
        settings.highlightKeywords = kwBuf;
        changed = true;
    }
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                       "Matching logcat lines are highlighted in yellow.");
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "ADB");
    ImGui::Indent();
    ImGui::TextWrapped("ADB in use: %s", settings.adbPath.empty()
                           ? "(auto-detected: bundled / SDK / PATH)"
                           : settings.adbPath.c_str());
    ImGui::Unindent();

    ImGui::Spacing();
    if (changed) {
        settings.save(Settings::defaultPath());
        if (settings.uiTheme == 0 || settings.uiTheme == 1) {
            LogCaterRequestTheme(settings.uiTheme);
        }
    }
}
