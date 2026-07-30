#include "UpdateChecker.h"
#define NOMINMAX
#include <Windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

// --- Version comparison ---

bool UpdateChecker::isNewer(const std::string& latestTag, const std::string& currentVersion) {
    auto parseVer = [](const std::string& s) -> std::vector<int> {
        std::vector<int> parts;
        std::istringstream iss(s);
        std::string tok;
        while (std::getline(iss, tok, '.')) {
            while (!tok.empty() && !std::isdigit(static_cast<unsigned char>(tok.front())))
                tok.erase(0, 1);
            if (tok.empty()) continue;
            try { parts.push_back(std::stoi(tok)); }
            catch (...) { parts.push_back(0); }
        }
        return parts;
    };

    auto latest = parseVer(latestTag);
    auto current = parseVer(currentVersion);
    size_t n = (std::max)(latest.size(), current.size());
    latest.resize(n, 0);
    current.resize(n, 0);

    for (size_t i = 0; i < n; i++) {
        if (latest[i] > current[i]) return true;
        if (latest[i] < current[i]) return false;
    }
    return false;
}

// --- JSON parsing ---

bool UpdateChecker::parseJson(const std::string& jsonStr, const std::string& currentVersion,
                               UpdateInfo& out) {
    try {
        auto j = json::parse(jsonStr);
        out.latestVersion = j.value("tag_name", "");
        out.downloadUrl = j.value("html_url", "");
        // Extract the first asset's download URL
        if (j.contains("assets") && j["assets"].is_array() && !j["assets"].empty()) {
            out.assetDownloadUrl = j["assets"][0].value("browser_download_url", "");
        }
        if (!out.latestVersion.empty() && isNewer(out.latestVersion, currentVersion)) {
            out.hasUpdate = true;
            return true;
        }
    } catch (...) {}
    return false;
}

// --- HTTP helper ---

static bool winHttpGet(const std::wstring& host, const std::wstring& path,
                       std::string& responseBody) {
    HINTERNET hSession = WinHttpOpen(L"LogCater/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    LPCWSTR acceptTypes[] = { L"application/vnd.github+json", nullptr };
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, acceptTypes, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false;
    }

    bool ok = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        responseBody.clear();
        DWORD bytesRead = 0;
        char buf[4096];
        while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
            responseBody.append(buf, bytesRead);
        }
        ok = true;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

// --- Check for updates ---

void UpdateChecker::check(const std::string& currentVersion,
                           std::function<void(const UpdateInfo&)> callback) {
    std::thread([currentVersion, cb = std::move(callback)]() {
        UpdateInfo info;
        std::string response;
        if (winHttpGet(L"api.github.com", L"/repos/kefengwei/LogCater/releases/latest", response)) {
            parseJson(response, currentVersion, info);
        }
        cb(info);
    }).detach();
}

// --- Download to file with progress ---

bool UpdateChecker::downloadFile(const std::string& url, const std::string& destPath,
                                  StatusCallback onStatus) {
    // Parse host and path from URL
    // URL format: https://github.com/.../releases/download/v1.0.1/LogCater.zip
    std::string urlStr = url;
    if (urlStr.rfind("https://", 0) != 0) return false;

    std::string rest = urlStr.substr(8); // skip "https://"
    size_t firstSlash = rest.find('/');
    if (firstSlash == std::string::npos) return false;

    std::string host = rest.substr(0, firstSlash);
    std::string path = rest.substr(firstSlash);

    // Convert to wide strings
    int hostLen = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
    std::wstring wHost(hostLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, &wHost[0], hostLen);

    int pathLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wPath(pathLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wPath[0], pathLen);

    HINTERNET hSession = WinHttpOpen(L"LogCater/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(),
        nullptr, WINHTTP_NO_REFERER, nullptr, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false;
    }

    // Follow redirects
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    bool ok = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {

        // Get content length
        DWORD contentLength = 0;
        DWORD dwSize = sizeof(contentLength);
        WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &dwSize, WINHTTP_NO_HEADER_INDEX);

        // Open output file
        std::ofstream outFile(destPath, std::ios::binary);
        if (!outFile.is_open()) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        DWORD downloaded = 0;
        DWORD bytesRead = 0;
        char buf[8192];
        while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
            outFile.write(buf, bytesRead);
            downloaded += bytesRead;
            if (contentLength > 0 && onStatus) {
                float progress = static_cast<float>(downloaded) / static_cast<float>(contentLength);
                if (progress > 1.0f) progress = 1.0f;
                onStatus("Downloading...", progress);
            }
        }
        outFile.close();
        ok = (downloaded > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

// --- Get app directory ---

std::string UpdateChecker::getAppDir() {
    char buf[MAX_PATH];
    if (GetModuleFileNameA(nullptr, buf, sizeof(buf))) {
        std::string path(buf);
        size_t lastSlash = path.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            return path.substr(0, lastSlash);
        }
    }
    return ".";
}

// --- Download and install ---

void UpdateChecker::downloadAndInstall(const std::string& url,
                                        const std::string& assetDownloadUrl,
                                        StatusCallback onStatus,
                                        DoneCallback onDone) {
    std::thread([url, assetDownloadUrl,
                 onStatus = std::move(onStatus),
                 onDone = std::move(onDone)]() {

        // Determine download URL (prefer asset, fallback to html_url)
        std::string downloadUrl = assetDownloadUrl.empty() ? url : assetDownloadUrl;
        if (downloadUrl.empty()) {
            onDone(false, "No download URL available");
            return;
        }

        // Temp paths
        char tempDir[MAX_PATH];
        GetTempPathA(sizeof(tempDir), tempDir);
        std::string zipPath = std::string(tempDir) + "LogCaterUpdate.zip";
        std::string extractDir = std::string(tempDir) + "LogCaterUpdate";
        std::string appDir = getAppDir();

        // Step 1: Download
        onStatus("Downloading update...", 0.0f);
        if (!downloadFile(downloadUrl, zipPath, onStatus)) {
            onDone(false, "Download failed. Check your network connection.");
            return;
        }

        // Step 2: Extract
        onStatus("Extracting...", -1.0f);

        // Remove old extract dir if exists
        std::string rmCmd = "rmdir /S /Q \"" + extractDir + "\" 2>nul";
        system(rmCmd.c_str());
        CreateDirectoryA(extractDir.c_str(), nullptr);

        // Use PowerShell to extract
        std::string psCmd = "powershell -NoProfile -Command \"Expand-Archive -Path '" +
                            zipPath + "' -DestinationPath '" + extractDir + "' -Force\"";
        int extractResult = system(psCmd.c_str());

        // The zip might have a wrapper directory. Find the actual content.
        // Our CI zip structure: LogCater/logcater.exe + LogCater/adb/
        // After extraction we get: extractDir/LogCater/logcater.exe
        // OR if wrapper is flattened: extractDir/logcater.exe
        // Walk into a single subdirectory if there is one.
        std::string contentDir = extractDir;
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA((extractDir + "\\*").c_str(), &fd);
        int subDirCount = 0;
        std::string singleSubDir;
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                        subDirCount++;
                        singleSubDir = fd.cFileName;
                    }
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
        if (subDirCount == 1 && !singleSubDir.empty()) {
            // Check if logcater.exe is inside this subdir
            std::string candidate = extractDir + "\\" + singleSubDir;
            if (GetFileAttributesA((candidate + "\\logcater.exe").c_str()) != INVALID_FILE_ATTRIBUTES) {
                contentDir = candidate;
            }
        }

        if (extractResult != 0) {
            // Try fallback with COM object
            onDone(false, "Extraction failed. Please update manually.");
            return;
        }

        // Step 3: Create updater batch script
        onStatus("Installing...", -1.0f);

        std::string batchPath = std::string(tempDir) + "LogCaterUpdater.bat";
        std::ofstream batch(batchPath);
        if (!batch.is_open()) {
            onDone(false, "Failed to create updater script.");
            return;
        }

        batch << "@echo off\r\n";
        batch << "rem Wait for LogCater to exit\r\n";
        batch << "timeout /t 2 /nobreak >nul\r\n";
        batch << "echo Updating LogCater...\r\n";
        // Copy new files
        batch << "xcopy /E /Y /Q \"" << contentDir << "\\*\" \"" << appDir << "\\\"\r\n";
        // Clean up temp files
        batch << "del /Q \"" << zipPath << "\" 2>nul\r\n";
        batch << "rmdir /S /Q \"" << extractDir << "\" 2>nul\r\n";
        batch << "del /Q \"" << batchPath << "\" 2>nul\r\n";
        // Restart
        batch << "echo Starting LogCater...\r\n";
        batch << "start \"\" \"" << appDir << "\\logcater.exe\"\r\n";
        batch.close();

        // Step 4: Launch updater and exit
        onStatus("Restarting...", 1.0f);

        // Launch the batch file detached
        ShellExecuteA(nullptr, "open", batchPath.c_str(),
                      nullptr, nullptr, SW_HIDE);

        onDone(true, "");
    }).detach();
}
