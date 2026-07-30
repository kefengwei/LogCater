#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <cstdint>

class AdbProcess;

struct DropboxEntry {
    std::string tag;
    int64_t timestampMillis = 0;
    std::string content;
    bool contentLoaded = false;
    std::string timeStr;
    int sizeBytes = 0;
};

class DropboxReader {
public:
    DropboxReader();
    ~DropboxReader();

    using EntriesCallback = std::function<void(std::vector<DropboxEntry>)>;
    using EntryCallback = std::function<void(std::string)>;

    void listEntries(const std::string& deviceSerial, EntriesCallback onDone);
    void getEntry(const std::string& deviceSerial,
                  const std::string& tag,
                  int64_t timestampMillis,
                  EntryCallback onDone);

    bool isBusy() const { return m_busy->load(); }

private:
    std::unique_ptr<AdbProcess> m_currentProcess;
    std::shared_ptr<std::atomic<bool>> m_busy; // shared_ptr for safe lambda capture
};
