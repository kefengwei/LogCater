#include "LogcatReader.h"
#include "core/AdbProcess.h"
#include "core/LogBuffer.h"

LogcatReader::LogcatReader()
    : m_lineRegex(R"(^(\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\s+(\d+)\s+(\d+)\s+([VDIWEF])\s+(\S+)\s*:\s?(.*)$)") {
}

LogcatReader::~LogcatReader() {
    stop();
}

void LogcatReader::start(const std::string& deviceSerial, LogBuffer& buffer,
                         const std::string& bufferName) {
    if (m_running.load()) return;

    m_buffer = &buffer;
    m_entryIndex.store(0);
    m_totalLines.store(0);
    m_parsedLines.store(0);
    m_skippedLines.store(0);
    m_process = std::make_unique<AdbProcess>();

    std::vector<std::string> args = {"-s", deviceSerial, "logcat", "-v", "threadtime"};
    if (!bufferName.empty()) {
        args.push_back("-b");
        args.push_back(bufferName);
    }
    bool ok = m_process->start(args,
        [this](const std::string& line) { onLogLine(line); });

    if (ok) {
        m_running.store(true);
    }
}

void LogcatReader::stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    if (m_process) {
        m_process->stop();
        m_process.reset();
    }
    m_buffer = nullptr;
}

void LogcatReader::onLogLine(const std::string& rawLine) {
    if (!m_buffer) return;

    m_totalLines.fetch_add(1);

    // Handle logcat markers like "--------- beginning of ..."
    if (rawLine.size() > 10 && rawLine[0] == '-' && rawLine[1] == '-') {
        m_skippedLines.fetch_add(1);
        return;
    }

    int64_t idx = m_entryIndex.fetch_add(1);
    auto entry = parseLine(rawLine, idx, m_lineRegex);

    if (!entry.tag.empty()) {
        m_parsedLines.fetch_add(1);
    }

    m_buffer->push(std::move(entry));
}

LogEntry LogcatReader::parseLine(const std::string& line, int64_t index, std::regex& re) {
    LogEntry entry;
    entry.index = index;
    entry.raw = line;

    std::smatch match;
    if (std::regex_search(line, match, re)) {
        // match[1]: timestamp "MM-DD HH:MM:SS.mmm"
        // match[2]: pid
        // match[3]: tid
        // match[4]: level (V/D/I/W/E/F)
        // match[5]: tag
        // match[6]: message
        entry.pid = std::stoi(match[2].str());
        entry.tid = std::stoi(match[3].str());
        entry.level = match[4].str()[0];
        entry.tag = match[5].str();
        entry.message = match[6].str();
    }

    return entry;
}
