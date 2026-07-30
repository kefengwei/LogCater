#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "imgui.h"

class AdbProcess;
class DeviceManager;
class Settings;

struct FileEntry {
    std::string name;
    std::string permissions;
    std::string size;
    bool isDir = false;
};

class FileBrowserPanel {
public:
    FileBrowserPanel();
    ~FileBrowserPanel();

    void render(const std::string& deviceSerial, DeviceManager& dm, Settings& settings);

private:
    // Package selection
    std::string m_selectedPackage;
    char m_packageFilterBuf[128] = {};

    // Current directory
    std::string m_currentPath;

    // File listing
    std::vector<FileEntry> m_entries;
    bool m_loading = false;
    std::string m_error;

    // File content viewer
    bool m_showContent = false;
    std::string m_contentTitle;
    std::string m_contentText;
    bool m_contentLoading = false;
    // Cached parsed lines for virtual scrolling (text, color)
    std::vector<std::pair<std::string, ImVec4>> m_contentLines;

    void navigateTo(const std::string& deviceSerial, const std::string& path);
    void goUp();
    void refreshListing(const std::string& deviceSerial);
    void viewFile(const std::string& deviceSerial, const std::string& path, const std::string& name);
    void pullFile(const std::string& deviceSerial, const std::string& path, const std::string& name);
    void uploadFile(const std::string& deviceSerial, const std::string& localPath);
    static bool runLs(const std::string& deviceSerial, const std::string& path,
                      std::vector<FileEntry>& out, std::string& error);
};
