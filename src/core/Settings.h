#pragma once

#include <string>
#include <vector>
#include <cstddef>

class Settings {
public:
    // Tag filter history
    std::vector<std::string> tagHistory;
    static constexpr size_t MAX_TAG_HISTORY = 50;

    // Window state
    float mainWindowPosX = -1, mainWindowPosY = -1;
    float mainWindowWidth = 1280, mainWindowHeight = 900;
    bool mainWindowMaximized = false;

    // Logcat panel
    std::string lastTextFilter;
    std::string lastTagFilter;
    char lastLevelFilter = 'V';
    bool logcatAutoScroll = true;
    int logcatBufferSize = 100'000;

    // Dropbox panel
    std::string lastDropboxTypeFilter;

    // ADB path
    std::string adbPath;

    // File browser bookmarks
    std::vector<std::string> fileBrowserBookmarks;
    static constexpr size_t MAX_BOOKMARKS = 20;

    // UI zoom
    float uiScale = 1.0f;

    // UI theme: 0 = dark, 1 = light
    int uiTheme = 0;

    // Logcat highlight keywords (comma separated)
    std::string highlightKeywords;

    void load(const std::string& path);
    void save(const std::string& path) const;
    void addTagToHistory(const std::string& tag);

    static std::string defaultPath();

private:
    mutable bool m_dirty = false;
};
