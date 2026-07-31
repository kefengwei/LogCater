#include "DeviceInfoPanel.h"
#include "core/AdbProcess.h"
#include "imgui.h"
#include <regex>
#include <sstream>
#include <thread>
#include <cmath>

// ─── Command helpers ───────────────────────────────────────────────

static std::string runAdb(const std::string& deviceSerial,
                          const std::vector<std::string>& args) {
    AdbProcess proc;
    std::string output;
    proc.start(args, [&](const std::string& line) {
        if (!output.empty()) output += "\n";
        output += line;
    });
    proc.waitForExit(5000);
    proc.stop();
    return output;
}

// ─── Parse helpers ─────────────────────────────────────────────────

void DeviceInfoPanel::parseBattery(const std::string& output, Info& info) {
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Format: "    key: value"
        std::regex re(R"(\s*(\S[^:]*):\s*(.*))");
        std::smatch m;
        if (std::regex_search(line, m, re)) {
            std::string key = m[1].str();
            std::string val = m[2].str();
            if (key == "level") info.batteryLevel = std::stoi(val);
            else if (key == "temperature") {
                int t = std::stoi(val);
                info.batteryTempC = t / 10.0f;
            }
            else if (key == "health") info.batteryHealth = val;
            else if (key == "status") info.batteryStatus = val;
        }
    }
}

void DeviceInfoPanel::parseDf(const std::string& output, Info& info) {
    // "adb shell df -h /data" output example:
    // Filesystem      Size  Used Avail Use% Mounted on
    // /dev/block/...   110G   45G   65G  41% /data
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find("/data") != std::string::npos &&
            line.find("Filesystem") == std::string::npos) {
            // Parse columns: Filesystem Size Used Avail Use% Mounted
            std::regex re(R"((\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\d+)%)");
            std::smatch m;
            if (std::regex_search(line, m, re)) {
                info.storageTotal  = m[2].str();
                info.storageUsed   = m[3].str();
                info.storageFree   = m[4].str();
                info.storagePercent = std::stoi(m[5].str());
            }
        }
    }
}

void DeviceInfoPanel::parseMeminfo(const std::string& output, Info& info) {
    // /proc/meminfo: "Key:  value kB"
    std::istringstream iss(output);
    std::string line;
    long totalKb = 0, availKb = 0;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::regex re(R"(^(\S+):\s+(\d+)\s)");
        std::smatch m;
        if (std::regex_search(line, m, re)) {
            std::string key = m[1].str();
            long val = std::stol(m[2].str());
            if (key == "MemTotal") totalKb = val;
            else if (key == "MemAvailable") availKb = val;
        }
    }

    // Format as human-readable
    auto fmt = [](long kb) -> std::string {
        if (kb >= 1048576) return std::to_string(kb / 1048576) + "." +
            std::to_string((kb % 1048576) * 10 / 1048576) + " GB";
        if (kb >= 1024) return std::to_string(kb / 1024) + " MB";
        return std::to_string(kb) + " KB";
    };
    info.memTotal = fmt(totalKb);
    info.memAvailable = fmt(availKb);
    if (totalKb > 0) {
        info.memPercent = static_cast<int>((totalKb - availKb) * 100 / totalKb);
    }
}

void DeviceInfoPanel::parseUptime(const std::string& output, Info& info) {
    // /proc/uptime: "12345.67 98765.43"
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::regex re(R"(^(\d+\.?\d*)\s)");
        std::smatch m;
        if (std::regex_search(line, m, re)) {
            double seconds = std::stod(m[1].str());
            int days = static_cast<int>(seconds) / 86400;
            int hours = (static_cast<int>(seconds) % 86400) / 3600;
            int mins = (static_cast<int>(seconds) % 3600) / 60;
            if (days > 0)
                info.uptime = std::to_string(days) + "d " + std::to_string(hours) + "h " + std::to_string(mins) + "m";
            else if (hours > 0)
                info.uptime = std::to_string(hours) + "h " + std::to_string(mins) + "m";
            else
                info.uptime = std::to_string(mins) + "m";
            break;
        }
    }
}

// ─── Refresh ───────────────────────────────────────────────────────

void DeviceInfoPanel::refresh(const std::string& deviceSerial) {
    if (m_loading.exchange(true)) return; // already running

    std::thread([this, deviceSerial]() {
        Info info;

        // System properties
        info.manufacturer = getProp(deviceSerial, "ro.product.manufacturer");
        info.model = getProp(deviceSerial, "ro.product.model");
        info.androidVersion = getProp(deviceSerial, "ro.build.version.release");
        info.sdkLevel = getProp(deviceSerial, "ro.build.version.sdk");
        info.buildFingerprint = getProp(deviceSerial, "ro.build.fingerprint");

        // Battery
        std::string batteryOut = runAdb(deviceSerial,
            {"-s", deviceSerial, "shell", "dumpsys", "battery"});
        parseBattery(batteryOut, info);

        // Storage
        std::string dfOut = runAdb(deviceSerial,
            {"-s", deviceSerial, "shell", "df", "-h", "/data"});
        parseDf(dfOut, info);

        // Memory
        std::string memOut = runAdb(deviceSerial,
            {"-s", deviceSerial, "shell", "cat", "/proc/meminfo"});
        parseMeminfo(memOut, info);

        // Uptime
        std::string uptimeOut = runAdb(deviceSerial,
            {"-s", deviceSerial, "shell", "cat", "/proc/uptime"});
        parseUptime(uptimeOut, info);

        info.valid = true;
        m_info = info;
        m_hasData = true;
        m_loading.store(false);
    }).detach();
}

std::string DeviceInfoPanel::getProp(const std::string& deviceSerial,
                                      const std::string& key) {
    std::string out = runAdb(deviceSerial,
        {"-s", deviceSerial, "shell", "getprop", key});
    // Trim trailing whitespace/newlines
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    return out;
}

// ─── UI Render ─────────────────────────────────────────────────────

void DeviceInfoPanel::renderProgressBar(int percent, const char* label,
                                         float r, float g, float b) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(80);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(r, g, b, 0.7f));
    ImGui::ProgressBar(percent / 100.0f, ImVec2(-1, 0), "");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("%d%%", percent);
}

void DeviceInfoPanel::render(const std::string& deviceSerial) {
    // Auto-refresh
    float now = static_cast<float>(ImGui::GetTime());
    if (m_hasData && now - m_lastRefreshTime > AUTO_REFRESH_INTERVAL) {
        m_lastRefreshTime = now;
        refresh(deviceSerial);
    }

    // Refresh button
    if (ImGui::Button("Refresh")) {
        m_lastRefreshTime = now;
        refresh(deviceSerial);
    }
    ImGui::SameLine();
    if (m_loading.load())
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Loading...");
    else if (m_hasData)
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Last updated: %.0fs ago",
                           now - m_lastRefreshTime);
    else
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No data — click Refresh");

    ImGui::Separator();
    if (!m_hasData) return;

    const Info& d = m_info;

    // ── System Info Card ──
    if (ImGui::CollapsingHeader("System", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("##sysinfo", 2, ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("");

            auto row = [](const char* label, const std::string& val) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(val.c_str());
            };
            row("Manufacturer", d.manufacturer);
            row("Model", d.model);
            row("Android", d.androidVersion + " (SDK " + d.sdkLevel + ")");
            row("Build", d.buildFingerprint);
            row("Uptime", d.uptime);
            ImGui::EndTable();
        }
    }

    // ── Battery Card ──
    if (ImGui::CollapsingHeader("Battery", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (d.batteryLevel >= 0) {
            float r = 0.4f, g = 0.9f, b = 0.4f;
            if (d.batteryLevel < 20) { r = 0.9f; g = 0.2f; b = 0.2f; }
            else if (d.batteryLevel < 50) { r = 1.0f; g = 0.7f; b = 0.0f; }
            renderProgressBar(d.batteryLevel, "Level", r, g, b);
        }
        if (d.batteryTempC >= 0) {
            float t = d.batteryTempC;
            ImGui::Text("Temperature: %.1f°C", t);
        }
        if (!d.batteryHealth.empty())
            ImGui::Text("Health: %s", d.batteryHealth.c_str());
        if (!d.batteryStatus.empty())
            ImGui::Text("Status: %s", d.batteryStatus.c_str());
    }

    // ── Storage Card ──
    if (ImGui::CollapsingHeader("Storage (/data)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (d.storagePercent >= 0) {
            float r = 0.4f, g = 0.9f, b = 0.4f;
            if (d.storagePercent > 90) { r = 0.9f; g = 0.2f; b = 0.2f; }
            else if (d.storagePercent > 70) { r = 1.0f; g = 0.7f; b = 0.0f; }
            renderProgressBar(d.storagePercent, "Used", r, g, b);
        }
        if (ImGui::BeginTable("##storage", 2, ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("");
            auto row = [](const char* label, const std::string& val) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(val.c_str());
            };
            row("Total", d.storageTotal);
            row("Used", d.storageUsed);
            row("Free", d.storageFree);
            ImGui::EndTable();
        }
    }

    // ── Memory Card ──
    if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (d.memPercent >= 0) {
            float r = 0.4f, g = 0.9f, b = 0.4f;
            if (d.memPercent > 90) { r = 0.9f; g = 0.2f; b = 0.2f; }
            else if (d.memPercent > 70) { r = 1.0f; g = 0.7f; b = 0.0f; }
            renderProgressBar(d.memPercent, "Used", r, g, b);
        }
        if (ImGui::BeginTable("##memory", 2, ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("");
            auto row = [](const char* label, const std::string& val) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(val.c_str());
            };
            row("Total", d.memTotal);
            row("Available", d.memAvailable);
            ImGui::EndTable();
        }
    }
}
