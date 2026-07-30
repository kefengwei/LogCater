#include "DropboxReader.h"
#include "core/AdbProcess.h"
#include "util/Timestamp.h"
#include <regex>
#include <thread>

DropboxReader::DropboxReader()
    : m_busy(std::make_shared<std::atomic<bool>>(false)) {
}

DropboxReader::~DropboxReader() {
    if (m_currentProcess) {
        m_currentProcess->stop();
    }
    *m_busy = true;
}

void DropboxReader::listEntries(const std::string& deviceSerial, EntriesCallback onDone) {
    if (m_busy->load()) return;
    m_busy->store(true);

    auto proc = std::make_shared<AdbProcess>();
    auto entries = std::make_shared<std::vector<DropboxEntry>>();
    auto busy = m_busy;

    proc->start(
        {"-s", deviceSerial, "shell", "dumpsys", "dropbox"},
        [entries, busy](const std::string& line) {
            if (line.empty()) return;
            if (line.size() > 4 && line[0] == '=' && line[1] == '=') return;

            // Format: "2026-07-27 18:56:11 system_app_wtf (text, 1969 bytes)"
            std::regex re(R"(^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s+(\S+)\s+\(([^,]+),\s*(\d+)\s*bytes?\))");
            std::smatch match;
            if (std::regex_search(line, match, re)) {
                DropboxEntry entry;
                entry.timeStr = match[1].str();
                entry.tag = match[2].str();
                entry.sizeBytes = std::stoi(match[4].str());
                entry.timestampMillis = util::parseDropboxTimestamp(entry.timeStr);
                entries->push_back(std::move(entry));
            }
        });

    std::thread([proc, entries, onDone = std::move(onDone), busy]() mutable {
        // Wait for process to exit naturally (up to 15s), then collect results
        proc->waitForExit(15000);
        // Ensure stopped
        proc->stop();
        if (onDone) {
            onDone(std::move(*entries));
        }
        busy->store(false);
    }).detach();
}

void DropboxReader::getEntry(const std::string& deviceSerial,
                              const std::string& tag,
                              int64_t timestampMillis,
                              EntryCallback onDone) {
    if (m_busy->load()) return;
    m_busy->store(true);

    auto sec = static_cast<std::time_t>(timestampMillis / 1000);
    std::tm tm_buf;
    localtime_s(&tm_buf, &sec);
    char timeArg[64];
    std::strftime(timeArg, sizeof(timeArg), "%Y-%m-%d %H:%M:%S", &tm_buf);

    auto proc = std::make_shared<AdbProcess>();
    auto content = std::make_shared<std::string>();
    auto busy = m_busy;

    proc->start(
        {"-s", deviceSerial, "shell", "dumpsys", "dropbox", "--print", tag, timeArg},
        [content](const std::string& line) {
            if (!content->empty()) *content += "\n";
            *content += line;
        });

    std::thread([proc, content, onDone = std::move(onDone), busy]() mutable {
        proc->waitForExit(10000);
        proc->stop();
        if (onDone) {
            onDone(*content);
        }
        busy->store(false);
    }).detach();
}
