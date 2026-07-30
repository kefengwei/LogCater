#pragma once

#include "adb/DropboxReader.h"
#include <vector>
#include <string>
#include <memory>
#include <mutex>

class Settings;

class DropboxPanel {
public:
    DropboxPanel();
    ~DropboxPanel();

    void render(Settings& settings, const std::string& deviceSerial);

private:
    DropboxReader m_reader;

    mutable std::mutex m_mutex;
    std::vector<DropboxEntry> m_entries;
    std::string m_typeFilter;
    bool m_hasEntries = false;
    bool m_loading = false;
    std::string m_selectedContent;
    std::string m_selectedTitle;
    bool m_showDetail = false;

    void fetchEntries(const std::string& deviceSerial);
    std::vector<std::string> getUniqueTypes() const;
};
