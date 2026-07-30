#include "Timestamp.h"
#include <ctime>
#include <cstdio>
#include <array>

namespace util {

int64_t parseLogcatTimestamp(const std::string& ts) {
    // Format: "07-30 12:34:56.789"
    int month, day, hour, min, sec, ms;
    if (std::sscanf(ts.c_str(), "%d-%d %d:%d:%d.%d",
                    &month, &day, &hour, &min, &sec, &ms) >= 6) {
        // Use current year
        auto now = std::time(nullptr);
        auto* tm = std::localtime(&now);
        int year = tm->tm_year + 1900;

        std::tm t = {};
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min = min;
        t.tm_sec = sec;

        auto epoch = std::mktime(&t);
        return static_cast<int64_t>(epoch) * 1000 + ms;
    }
    return 0;
}

std::string formatTimestamp(int64_t ms) {
    auto sec = static_cast<std::time_t>(ms / 1000);
    auto* tm = std::localtime(&sec);
    std::array<char, 64> buf;
    std::strftime(buf.data(), buf.size(), "%m-%d %H:%M:%S", tm);
    return std::string(buf.data());
}

int64_t parseDropboxTimestamp(const std::string& ts) {
    // Format: "2026-07-30 12:34:56"
    int year, month, day, hour, min, sec;
    if (std::sscanf(ts.c_str(), "%d-%d-%d %d:%d:%d",
                    &year, &month, &day, &hour, &min, &sec) >= 6) {
        std::tm t = {};
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min = min;
        t.tm_sec = sec;

        auto epoch = std::mktime(&t);
        return static_cast<int64_t>(epoch) * 1000;
    }
    return 0;
}

} // namespace util
