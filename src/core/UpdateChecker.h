#pragma once

#include <string>
#include <functional>

class UpdateChecker {
public:
    struct UpdateInfo {
        bool hasUpdate = false;
        std::string latestVersion;    // e.g. "v1.0.1"
        std::string downloadUrl;      // browser URL (fallback)
        std::string assetDownloadUrl; // actual zip download URL
    };

    /// Check GitHub Releases API asynchronously.
    static void check(const std::string& currentVersion,
                      std::function<void(const UpdateInfo&)> callback);

    /// Status callback: status text, 0..1 progress (-1 = indeterminate)
    using StatusCallback = std::function<void(const std::string& status, float progress)>;
    /// Done callback: success, error message (empty on success)
    using DoneCallback = std::function<void(bool success, const std::string& error)>;

    /// Download and install an update. Calls status/progress, then onDone.
    /// On success, the app will exit and the updater batch script takes over.
    static void downloadAndInstall(const std::string& url,
                                   const std::string& assetDownloadUrl,
                                   StatusCallback onStatus,
                                   DoneCallback onDone);

private:
    static bool parseJson(const std::string& json, const std::string& currentVersion,
                          UpdateInfo& out);
    static bool isNewer(const std::string& latestTag, const std::string& currentVersion);
    static std::string getAppDir();
    static bool downloadFile(const std::string& url, const std::string& destPath,
                             StatusCallback onStatus);
};
