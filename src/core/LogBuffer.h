#pragma once

#include <string>
#include <vector>
#include <shared_mutex>
#include <cstdint>
#include <cstddef>

struct LogEntry {
    int64_t index = 0;
    std::string raw;
    std::string tag;
    std::string message;
    std::string processName; // from ps -A lookup
    char level = 'V';
    int pid = 0;
    int tid = 0;
};

class LogBuffer {
public:
    static constexpr size_t DEFAULT_CAPACITY = 100'000;

    explicit LogBuffer(size_t capacity = DEFAULT_CAPACITY);

    void push(LogEntry entry);
    void clear();

    /// Query: copies matching entries (newest first, then reversed to oldest→newest).
    /// @param levelMask  bitmask: bit0=V, bit1=D, bit2=I, bit3=W, bit4=E, bit5=F
    /// @param maxResults  if > 0, stops after collecting this many (most recent).
    /// @param timeFrom   "HH:MM:SS" — only entries at/after this time (empty = no filter).
    void query(std::vector<LogEntry>& out,
               const std::string& textFilter = "",
               const std::string& tagFilter = "",
               uint8_t levelMask = 0x3F,
               size_t maxResults = 0,
               const std::string& timeFrom = "",
               const std::string& excludeTag = "") const;

    /// Query: stores only ring-buffer positions of matching entries.
    void queryPositions(std::vector<size_t>& out,
                        const std::string& textFilter = "",
                        const std::string& tagFilter = "",
                        uint8_t levelMask = 0x3F,
                        const std::string& timeFrom = "",
                        const std::string& excludeTag = "") const;

    /// Batch-copy entries at given ring positions into out.
    /// out is cleared first, then filled with copies at positions [start, start+count).
    void getEntries(std::vector<LogEntry>& out,
                    const std::vector<size_t>& positions,
                    size_t start, size_t count) const;

    size_t size() const;
    /// Monotonic total number of entries ever pushed (never shrinks, unlike size()).
    size_t totalPushed() const;
    bool empty() const;
    bool overflowed() const { return m_overflowed; }
    size_t capacity() const { return m_ring.size(); }

private:
    mutable std::shared_mutex m_mutex;
    std::vector<LogEntry> m_ring;
    size_t m_writePos = 0;
    size_t m_totalCount = 0;
    bool m_overflowed = false;

    static int levelToInt(char level);

    /// Shared filter logic: returns true if entry passes filters.
    static bool matches(const LogEntry& entry,
                        const std::string& textFilter,
                        const std::string& tagFilter,
                        uint8_t levelMask,
                        const std::string& timeFrom,
                        const std::string& excludeTag);
};
