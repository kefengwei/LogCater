#pragma once

#include <string>
#include <functional>

class UpdateChecker {
public:
    struct UpdateInfo {
        bool hasUpdate = false;
        std::string latestVersion;   // e.g. "v1.0.1"
        std::string downloadUrl;     // browser URL
    };

    /// Check GitHub Releases API asynchronously. Calls callback when done.
    /// @param currentVersion  e.g. "1.0.0" (without 'v' prefix)
    static void check(const std::string& currentVersion,
                      std::function<void(UpdateInfo)> callback);

private:
    static bool parseJson(const std::string& json, const std::string& currentVersion,
                          UpdateInfo& out);
    static bool isNewer(const std::string& latestTag, const std::string& currentVersion);
};
