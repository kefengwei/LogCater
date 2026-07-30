#include "UpdateChecker.h"
#define NOMINMAX
#include <Windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

bool UpdateChecker::isNewer(const std::string& latestTag, const std::string& currentVersion) {
    // Parse version numbers: "v1.0.1" or "1.0.0"
    auto parseVer = [](const std::string& s) -> std::vector<int> {
        std::vector<int> parts;
        std::istringstream iss(s);
        std::string tok;
        while (std::getline(iss, tok, '.')) {
            // Strip leading 'v' or non-digit prefix
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

    size_t n = std::max(latest.size(), current.size());
    latest.resize(n, 0);
    current.resize(n, 0);

    for (size_t i = 0; i < n; i++) {
        if (latest[i] > current[i]) return true;
        if (latest[i] < current[i]) return false;
    }
    return false; // same version
}

bool UpdateChecker::parseJson(const std::string& jsonStr, const std::string& currentVersion,
                               UpdateInfo& out) {
    try {
        auto j = json::parse(jsonStr);
        out.latestVersion = j.value("tag_name", "");
        out.downloadUrl = j.value("html_url", "");
        if (!out.latestVersion.empty() && isNewer(out.latestVersion, currentVersion)) {
            out.hasUpdate = true;
            return true;
        }
    } catch (...) {}
    return false;
}

void UpdateChecker::check(const std::string& currentVersion,
                           std::function<void(UpdateInfo)> callback) {
    std::thread([currentVersion, cb = std::move(callback)]() {
        UpdateInfo info;

        // Open session
        HINTERNET hSession = WinHttpOpen(
            L"LogCater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) { cb(info); return; }

        // Connect to api.github.com
        HINTERNET hConnect = WinHttpConnect(hSession,
            L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); cb(info); return; }

        // Open request
        LPCWSTR acceptTypes[] = { L"application/vnd.github+json", nullptr };
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET",
            L"/repos/kefengwei/LogCater/releases/latest",
            nullptr, WINHTTP_NO_REFERER, acceptTypes,
            WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            cb(info);
            return;
        }

        // Send request
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            cb(info);
            return;
        }

        if (!WinHttpReceiveResponse(hRequest, nullptr)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            cb(info);
            return;
        }

        // Read response
        std::string response;
        DWORD bytesRead = 0;
        char buf[4096];
        while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
            response.append(buf, bytesRead);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        if (!response.empty()) {
            parseJson(response, currentVersion, info);
        }

        cb(info);
    }).detach();
}
