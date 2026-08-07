#include "Settings.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <Windows.h>
#include <ShlObj.h>

using json = nlohmann::json;

std::string Settings::defaultPath() {
    char appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath))) {
        std::string path = std::string(appDataPath) + "\\LogCater";
        CreateDirectoryA(path.c_str(), nullptr);
        return path + "\\settings.json";
    }
    return "settings.json";
}

void Settings::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    try {
        json j;
        file >> j;

        if (j.contains("tagHistory")) {
            tagHistory = j["tagHistory"].get<std::vector<std::string>>();
        }
        if (j.contains("mainWindowPosX")) mainWindowPosX = j["mainWindowPosX"];
        if (j.contains("mainWindowPosY")) mainWindowPosY = j["mainWindowPosY"];
        if (j.contains("mainWindowWidth")) mainWindowWidth = j["mainWindowWidth"];
        if (j.contains("mainWindowHeight")) mainWindowHeight = j["mainWindowHeight"];
        if (j.contains("mainWindowMaximized")) mainWindowMaximized = j["mainWindowMaximized"];
        if (j.contains("lastTextFilter")) lastTextFilter = j["lastTextFilter"];
        if (j.contains("lastTagFilter")) lastTagFilter = j["lastTagFilter"];
        if (j.contains("lastLevelFilter")) lastLevelFilter = j["lastLevelFilter"].get<char>();
        if (j.contains("logcatAutoScroll")) logcatAutoScroll = j["logcatAutoScroll"];
        if (j.contains("logcatBufferSize")) logcatBufferSize = j["logcatBufferSize"];
        if (j.contains("lastDropboxTypeFilter")) lastDropboxTypeFilter = j["lastDropboxTypeFilter"];
        if (j.contains("adbPath")) adbPath = j["adbPath"];
        if (j.contains("fileBrowserBookmarks")) {
            fileBrowserBookmarks = j["fileBrowserBookmarks"].get<std::vector<std::string>>();
        }
        if (j.contains("uiScale")) uiScale = j["uiScale"];
        if (j.contains("uiTheme")) uiTheme = j["uiTheme"];
        if (j.contains("highlightKeywords")) highlightKeywords = j["highlightKeywords"];
    } catch (...) {
        // Corrupted settings file, use defaults
    }
}

void Settings::save(const std::string& path) const {
    try {
        json j;
        j["tagHistory"] = tagHistory;
        j["mainWindowPosX"] = mainWindowPosX;
        j["mainWindowPosY"] = mainWindowPosY;
        j["mainWindowWidth"] = mainWindowWidth;
        j["mainWindowHeight"] = mainWindowHeight;
        j["mainWindowMaximized"] = mainWindowMaximized;
        j["lastTextFilter"] = lastTextFilter;
        j["lastTagFilter"] = lastTagFilter;
        j["lastLevelFilter"] = std::string(1, lastLevelFilter);
        j["logcatAutoScroll"] = logcatAutoScroll;
        j["logcatBufferSize"] = logcatBufferSize;
        j["lastDropboxTypeFilter"] = lastDropboxTypeFilter;
        j["adbPath"] = adbPath;
        j["fileBrowserBookmarks"] = fileBrowserBookmarks;
        j["uiScale"] = uiScale;
        j["uiTheme"] = uiTheme;
        j["highlightKeywords"] = highlightKeywords;

        std::ofstream file(path);
        if (file.is_open()) {
            file << j.dump(2);
        }
    } catch (...) {
        // Failed to save
    }
}

void Settings::addTagToHistory(const std::string& tag) {
    auto it = std::find(tagHistory.begin(), tagHistory.end(), tag);
    if (it != tagHistory.end()) {
        tagHistory.erase(it);
    }
    tagHistory.insert(tagHistory.begin(), tag);
    while (tagHistory.size() > MAX_TAG_HISTORY) {
        tagHistory.pop_back();
    }
    m_dirty = true;
}
