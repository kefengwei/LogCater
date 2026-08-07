#include "LogBuffer.h"
#include <algorithm>
#include <cctype>

LogBuffer::LogBuffer(size_t capacity) {
    m_ring.resize(capacity);
}

void LogBuffer::push(LogEntry entry) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    entry.index = static_cast<int64_t>(m_totalCount++);
    m_ring[m_writePos] = std::move(entry);
    m_writePos = (m_writePos + 1) % m_ring.size();
    if (m_totalCount > m_ring.size()) {
        m_overflowed = true;
    }
}

void LogBuffer::clear() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_writePos = 0;
    m_totalCount = 0;
    m_overflowed = false;
}

size_t LogBuffer::size() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return std::min(m_totalCount, m_ring.size());
}

size_t LogBuffer::totalPushed() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_totalCount;
}

bool LogBuffer::empty() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_totalCount == 0;
}

int LogBuffer::levelToInt(char level) {
    switch (level) {
        case 'V': return 0;
        case 'D': return 1;
        case 'I': return 2;
        case 'W': return 3;
        case 'E': return 4;
        case 'F': return 5;
        default:  return 0;
    }
}

bool LogBuffer::matches(const LogEntry& entry,
                         const std::string& textFilter,
                         const std::string& tagFilter,
                         uint8_t levelMask,
                         const std::string& timeFrom,
                         const std::string& excludeTag) {
    // Excluded tag: skip entries with this exact tag
    if (!excludeTag.empty() && entry.tag == excludeTag) return false;
    // Multi-tag filter: split by ';', match if entry tag matches ANY (OR logic).
    // Whitespace around tags is trimmed; empty segments are ignored.
    if (!tagFilter.empty()) {
        auto trim = [](const std::string& s) -> std::string {
            size_t b = 0;
            while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) b++;
            size_t e = s.size();
            while (e > b && (s[e-1] == ' ' || s[e-1] == '\t')) e--;
            return s.substr(b, e - b);
        };

        bool matched = false;
        size_t start = 0;
        while (start <= tagFilter.size()) {
            size_t end = tagFilter.find(';', start);
            if (end == std::string::npos) end = tagFilter.size();
            std::string segment = trim(tagFilter.substr(start, end - start));
            if (!segment.empty() && entry.tag == segment) {
                matched = true;
                break;
            }
            start = end + 1;
        }
        if (!matched) return false;
    }
    // Check level bitmask
    if (!(levelMask & (1 << levelToInt(entry.level)))) return false;
    // Time filter: raw starts with "MM-DD HH:MM:SS.mmm ..."; time is at offset 6, length 8.
    if (!timeFrom.empty()) {
        if (entry.raw.size() < 14) return false;
        if (entry.raw.substr(6, 8) < timeFrom) return false;
    }
    if (!textFilter.empty()) {
        const char* needle = textFilter.c_str();
        int ndlLen = static_cast<int>(textFilter.size());

        auto contains = [&](const char* str, int strLen) -> bool {
            if (ndlLen > strLen) return false;
            for (int i = 0; i <= strLen - ndlLen; i++) {
                int j;
                for (j = 0; j < ndlLen; j++) {
                    char hc = static_cast<char>(std::tolower(static_cast<unsigned char>(str[i + j])));
                    char nc = static_cast<char>(std::tolower(static_cast<unsigned char>(needle[j])));
                    if (hc != nc) break;
                }
                if (j == ndlLen) return true;
            }
            return false;
        };

        bool inRaw = contains(entry.raw.c_str(), static_cast<int>(entry.raw.size()));
        bool inProc = contains(entry.processName.c_str(), static_cast<int>(entry.processName.size()));
        if (!inRaw && !inProc) return false;
    }
    return true;
}

void LogBuffer::queryPositions(std::vector<size_t>& out,
                                const std::string& textFilter,
                                const std::string& tagFilter,
                                uint8_t levelMask,
                                const std::string& timeFrom,
                                const std::string& excludeTag) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);

    size_t count = std::min(m_totalCount, m_ring.size());
    if (count == 0) return;

    size_t startPos = (m_totalCount > m_ring.size()) ? m_writePos : 0;

    if (out.capacity() < count) {
        out.reserve(count);
    }

    for (size_t i = 0; i < count; i++) {
        size_t idx = (startPos + i) % m_ring.size();
        if (matches(m_ring[idx], textFilter, tagFilter, levelMask, timeFrom, excludeTag)) {
            out.push_back(idx);
        }
    }
}

void LogBuffer::getEntries(std::vector<LogEntry>& out,
                            const std::vector<size_t>& positions,
                            size_t start, size_t count) const {
    out.clear();
    if (start >= positions.size()) return;
    size_t end = std::min(start + count, positions.size());

    std::shared_lock<std::shared_mutex> lock(m_mutex);
    out.reserve(end - start);
    for (size_t i = start; i < end; i++) {
        out.push_back(m_ring[positions[i]]);
    }
}

void LogBuffer::query(std::vector<LogEntry>& out,
                      const std::string& textFilter,
                      const std::string& tagFilter,
                      uint8_t levelMask,
                      size_t maxResults,
                      const std::string& timeFrom,
                      const std::string& excludeTag) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);

    size_t count = std::min(m_totalCount, m_ring.size());
    if (count == 0) return;

    size_t newestPos = (m_writePos == 0) ? m_ring.size() - 1 : m_writePos - 1;

    std::vector<LogEntry> temp;
    size_t reserveSize = maxResults > 0 ? std::min(count, maxResults) : count;
    if (temp.capacity() < reserveSize) {
        temp.reserve(reserveSize);
    }

    for (size_t i = 0; i < count; i++) {
        size_t idx = (newestPos + m_ring.size() - i) % m_ring.size();
        if (matches(m_ring[idx], textFilter, tagFilter, levelMask, timeFrom, excludeTag)) {
            temp.push_back(m_ring[idx]);
            if (maxResults > 0 && temp.size() >= maxResults) break;
        }
    }

    out.insert(out.end(), temp.rbegin(), temp.rend());
}
