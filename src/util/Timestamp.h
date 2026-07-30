#pragma once

#include <string>
#include <cstdint>

namespace util {

/// Parse "MM-DD HH:MM:SS.mmm" to milliseconds since epoch (local time, approximate).
/// Returns a rough timestamp for sorting. Uses a fixed date reference.
int64_t parseLogcatTimestamp(const std::string& ts);

/// Format milliseconds as "MM-DD HH:MM:SS"
std::string formatTimestamp(int64_t ms);

/// Parse dropbox date "YYYY/MM/DD HH:MM:SS" to milliseconds since epoch.
int64_t parseDropboxTimestamp(const std::string& ts);

} // namespace util
