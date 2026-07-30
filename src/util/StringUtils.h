#pragma once

#include <string>

namespace util {

/// Case-insensitive substring search.
bool icontains(const std::string& haystack, const std::string& needle);

/// Trim leading whitespace.
std::string ltrim(const std::string& s);

/// Trim trailing whitespace.
std::string rtrim(const std::string& s);

/// Trim both ends.
std::string trim(const std::string& s);

} // namespace util
