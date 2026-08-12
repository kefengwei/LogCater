#pragma once

#include "core/LogBuffer.h"
#include "adb/LogcatReader.h"
#include "ui/FilterBar.h"
#include <memory>
#include <vector>
#include <string>

class DeviceManager;
class Settings;

class LogcatPanel {
public:
    LogcatPanel();
    ~LogcatPanel();

    void start(const std::string& deviceSerial);
    void stop();
    void setDeviceManager(DeviceManager* dm) { m_dm = dm; }
    bool isRunning() const;
    void onDeviceDisconnected();
    void refreshProcessNames();

    void pause();
    void resume();
    bool isPaused() const { return m_paused; }
    void clearLogs();
    void exportLogs();

    FilterBar& filterBar() { return m_filterBar; }

private:
    LogBuffer m_logBuffer;
    LogcatReader m_reader;
    FilterBar m_filterBar;
    DeviceManager* m_dm = nullptr;

    // Scroll state
    bool m_autoScroll = true;
    bool m_userScrolledSinceDisable = false;

    bool m_paused = false;
    std::string m_pausedDeviceSerial;
    std::string m_currentSerial;   // device currently streamed from
    std::string m_bufferName;      // "main" | "system" | "crash" | "all" | "" (default)
    int m_selectedIndex = -1;
    bool m_showDetail = false;
    int m_contextIndex = -1;
    std::vector<int> m_selectedRows;   // sorted selected row indices (multi-select)
    int m_selectionAnchor = -1;        // anchor for Shift+click range selection

    std::vector<LogEntry> m_displayEntries;
    static constexpr size_t MAX_DISPLAY = 10000;
    size_t m_pendingNew = 0;

    std::string m_lastTextFilter;
    std::string m_lastTagFilter;
    std::string m_lastTimeFrom;
    std::string m_lastExcludeTag;
    uint8_t m_lastLevelMask = 0x3F;
    size_t m_lastPushed = 0;
    double m_lastRefreshTime = -1.0;
    static constexpr double MIN_REFRESH_INTERVAL = 0.15;

    bool filtersChanged() const;
    bool dataChanged() const;
    void refreshDisplay();
    void enableAutoScroll();
    void copySelectedRows();

public:
    void render(Settings& settings);
};
