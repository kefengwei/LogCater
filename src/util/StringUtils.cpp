#include "StringUtils.h"
#include <algorithm>
#include <cctype>

namespace util {

bool icontains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (haystack.empty()) return false;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) { return std::tolower(a) == std::tolower(b); });
    return it != haystack.end();
}

std::string ltrim(const std::string& s) {
    auto start = std::find_if_not(s.begin(), s.end(),
        [](unsigned char c) { return std::isspace(c); });
    return std::string(start, s.end());
}

std::string rtrim(const std::string& s) {
    auto end = std::find_if_not(s.rbegin(), s.rend(),
        [](unsigned char c) { return std::isspace(c); });
    return std::string(s.begin(), end.base());
}

std::string trim(const std::string& s) {
    return rtrim(ltrim(s));
}

} // namespace util
