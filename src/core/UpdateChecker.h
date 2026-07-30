#pragma once

#include <string>
#include <functional>

class UpdateChecker {
public:
    struct UpdateInfo {
        bool hasUpdate = false;
        std::string latestVersion;  // e.g. "v1.1.0"
        std::string downloadUrl;    // browser URL to the release page
    };

    /// Check GitHub Releases API asynchronously.
    /// Calls callback with update info on the calling thread (via detached std::thread).
    static void check(const std::string& currentVersion,
                      std::function<void(const UpdateInfo&)> callback);

private:
    static bool parseJson(const std::string& json, const std::string& currentVersion,
                          UpdateInfo& out);
    static bool isNewer(const std::string& latestTag, const std::string& currentVersion);
};
