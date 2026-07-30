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
                         uint8_t levelMask) {
    if (!tagFilter.empty() && entry.tag != tagFilter) return false;
    // Check level bitmask
    if (!(levelMask & (1 << levelToInt(entry.level)))) return false;
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
                                uint8_t levelMask) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);

    size_t count = std::min(m_totalCount, m_ring.size());
    if (count == 0) return;

    size_t startPos = (m_totalCount > m_ring.size()) ? m_writePos : 0;

    if (out.capacity() < count) {
        out.reserve(count);
    }

    for (size_t i = 0; i < count; i++) {
        size_t idx = (startPos + i) % m_ring.size();
        if (matches(m_ring[idx], textFilter, tagFilter, levelMask)) {
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
                      size_t maxResults) const {
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
        if (matches(m_ring[idx], textFilter, tagFilter, levelMask)) {
            temp.push_back(m_ring[idx]);
            if (maxResults > 0 && temp.size() >= maxResults) break;
        }
    }

    out.insert(out.end(), temp.rbegin(), temp.rend());
}
