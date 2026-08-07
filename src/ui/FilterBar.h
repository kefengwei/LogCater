#pragma once

#include <string>
#include <vector>
#include <cstdint>

class Settings;

class FilterBar {
public:
    void render(Settings& settings);

    const std::string& textFilter() const { return m_textFilter; }
    const std::string& tagFilter() const { return m_tagFilter; }
    const std::string& timeFromFilter() const { return m_timeFromFilter; }
    const std::string& excludeTag() const { return m_excludeTag; }
    void setExcludeTag(const std::string& t) { m_excludeTag = t; }
    void clearExcludeTag() { m_excludeTag.clear(); }

    /// Bitmask of enabled log levels: bit 0=V, 1=D, 2=I, 3=W, 4=E, 5=F
    uint8_t levelMask() const { return m_levelMask; }

private:
    std::string m_textFilter;
    std::string m_tagFilter;
    std::string m_timeFromFilter;
    std::string m_excludeTag;
    uint8_t m_levelMask = 0x3F; // all levels enabled by default
    char m_tagBuf[256] = {};
    char m_textBuf[256] = {};
    char m_timeFromBuf[16] = {};
};
