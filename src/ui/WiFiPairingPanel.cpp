#include "WiFiPairingPanel.h"
#include "adb/DeviceManager.h"
#include "core/AdbProcess.h"
#include "QrCode.hpp"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include <GL/gl.h>
#include <algorithm>
#include <cctype>
#include <random>
#include <regex>

// ─── Random string generation ─────────────────────────────────────

namespace {

// Characters safe for WIFI QR format (excludes WIFI-format specials: \;,")
const char* RANDOM_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!#$%&'()*+-./:<=>?@[]^_{|}~";

// Per-thread RNG seeded from random_device
std::string randomString(size_t len) {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, std::strlen(RANDOM_CHARS) - 1);
    std::string result;
    result.reserve(len);
    for (size_t i = 0; i < len; i++)
        result += RANDOM_CHARS[dist(rng)];
    return result;
}

// ─── QR texture helpers ───────────────────────────────────────────

unsigned int createQrTexture(const qrcodegen::QrCode& qr, int modulePx, int quietModules,
                              int& outWidth, int& outHeight) {
    const int qrSize = qr.size;
    const int border = quietModules * modulePx;
    const int imgSize = qrSize * modulePx + border * 2;
    outWidth = imgSize;
    outHeight = imgSize;

    // Build RGBA pixel buffer
    std::vector<uint8_t> pixels(imgSize * imgSize * 4);
    for (int y = 0; y < imgSize; y++) {
        for (int x = 0; x < imgSize; x++) {
            // Map pixel to QR module
            int mx = (x - border) / modulePx;
            int my = (y - border) / modulePx;
            bool dark = false;
            if (mx >= 0 && mx < qrSize && my >= 0 && my < qrSize)
                dark = qr.getModule(mx, my);

            int idx = (y * imgSize + x) * 4;
            uint8_t c = dark ? 0 : 255;
            pixels[idx + 0] = c; // R
            pixels[idx + 1] = c; // G
            pixels[idx + 2] = c; // B
            pixels[idx + 3] = 255; // A
        }
    }

    // Create OpenGL texture
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imgSize, imgSize, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    return tex;
}

} // anonymous namespace

// ─── WiFiPairingPanel ─────────────────────────────────────────────

WiFiPairingPanel::WiFiPairingPanel() = default;

WiFiPairingPanel::~WiFiPairingPanel() {
    destroyQrTexture();
    if (m_bgThread.joinable())
        m_bgThread.join();
}

void WiFiPairingPanel::open() {
    if (m_open) return;
    m_open = true;
    m_state = State::Ready;
    m_statusMsg = "";
    m_errorMsg = "";
    m_bgDone.store(false);
    m_bgSuccess.store(false);
    m_justConnected = false;

    // Reset pair-code inputs
    m_ipPortInput[0] = '\0';
    m_pairCodeInput[0] = '\0';
    m_connectPortInput[0] = '\0';
    m_userProvidedConnectPort = false;
    m_pairingState = PairingState::Idle;
    m_connectRetries = 0;
    m_connectRetryTime = 0.0f;

    generateCredentials();
    buildQrTexture();

    // Reset discovery state
    m_lastMdnsTime = 0.0f;
    m_pairingIp.clear();
    m_pairingPort = 0;
    m_connectPort = 0;
}

void WiFiPairingPanel::close() {
    // Join any background adb thread before clearing state.
    // This prevents the bg thread from accessing members after close.
    if (m_bgThread.joinable())
        m_bgThread.join();
    m_open = false;
    m_state = State::Idle;
    destroyQrTexture();
}

bool WiFiPairingPanel::justConnected() const {
    return m_justConnected;
}

void WiFiPairingPanel::clearConnectedFlag() {
    m_justConnected = false;
}

void WiFiPairingPanel::generateCredentials() {
    m_serviceName = "studio-" + randomString(10);
    m_password = randomString(12);
    m_qrString = "WIFI:T:ADB;S:" + m_serviceName + ";P:" + m_password + ";;";
}

void WiFiPairingPanel::buildQrTexture() {
    destroyQrTexture();

    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(
        m_qrString.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);

    m_qrTexture = createQrTexture(qr, 8, 4, m_qrTexWidth, m_qrTexHeight);
}

void WiFiPairingPanel::destroyQrTexture() {
    if (m_qrTexture) {
        glDeleteTextures(1, &m_qrTexture);
        m_qrTexture = 0;
    }
}

// ─── mDNS discovery ───────────────────────────────────────────────

bool WiFiPairingPanel::tryDiscover() {
    // Run "adb mdns services" and parse output looking for our service name
    AdbProcess proc;
    std::vector<std::string> outputLines;
    proc.start({"mdns", "services"}, [&](const std::string& line) {
        outputLines.push_back(line);
    });

    proc.waitForExit(5000);
    proc.stop();

    // Parse lines looking for our service instance name
    // Output format: <serviceName> <serviceType> <ip>:<port>
    // Service types: _adb-tls-pairing._tcp, _adb-tls-connect._tcp
    for (const auto& line : outputLines) {
        if (line.find(m_serviceName) == std::string::npos)
            continue;
        if (line.find("_adb-tls-pairing._tcp") == std::string::npos)
            continue;

        // Extract IP:port
        std::regex ipPortRe(R"((\d+\.\d+\.\d+\.\d+):(\d+))");
        std::smatch m;
        if (std::regex_search(line, m, ipPortRe)) {
            m_pairingIp = m[1].str();
            m_pairingPort = std::stoi(m[2].str());
            return true;
        }
    }
    return false;
}

bool WiFiPairingPanel::tryDiscoverConnect() {
    AdbProcess proc;
    std::vector<std::string> outputLines;
    proc.start({"mdns", "services"}, [&](const std::string& line) {
        outputLines.push_back(line);
    });
    proc.waitForExit(5000);
    proc.stop();

    for (const auto& line : outputLines) {
        // Look for our device's connect service (same IP as pairing)
        if (line.find(m_pairingIp) == std::string::npos)
            continue;
        if (line.find("_adb-tls-connect._tcp") == std::string::npos)
            continue;

        std::regex ipPortRe(R"((\d+\.\d+\.\d+\.\d+):(\d+))");
        std::smatch m;
        if (std::regex_search(line, m, ipPortRe)) {
            m_connectPort = std::stoi(m[2].str());
            return true;
        }
    }
    return false;
}

// ─── Background adb commands ──────────────────────────────────────

void WiFiPairingPanel::runAdbBg(std::vector<std::string> args, std::string stdinData) {
    if (m_bgThread.joinable())
        m_bgThread.join();

    m_bgDone.store(false);
    m_bgSuccess.store(false);

    {
        std::lock_guard<std::mutex> lock(m_bgMutex);
        m_bgOutput.clear();
    }

    m_bgThread = std::thread([this, args = std::move(args),
                              stdinData = std::move(stdinData)]() {
        AdbProcess proc;
        std::string output;

        if (stdinData.empty()) {
            proc.start(args, [&](const std::string& line) {
                output += line + "\n";
            });
        } else {
            proc.startWithInput(args, [&](const std::string& line) {
                output += line + "\n";
            }, stdinData + "\n");
        }

        bool exited = proc.waitForExit(15000);
        proc.stop();

        {
            std::lock_guard<std::mutex> lock(m_bgMutex);
            m_bgOutput = output;
        }

        m_bgSuccess.store(exited);
        m_bgDone.store(true);
    });
}

void WiFiPairingPanel::switchMode(Mode mode) {
    if (mode == m_mode) return;
    // Don't allow switching while a pairing/connection is in flight
    if (m_state == State::Pairing || m_state == State::Connecting) return;

    m_mode = mode;
    m_state = State::Ready;
    m_statusMsg = "";
    m_errorMsg = "";
    m_bgDone.store(false);
    m_bgSuccess.store(false);
    m_lastMdnsTime = 0.0f;
    m_pairingIp.clear();
    m_pairingPort = 0;
    m_connectPort = 0;
    m_userProvidedConnectPort = false;
    m_pairingState = PairingState::Idle;
    m_connectRetries = 0;
    m_connectRetryTime = 0.0f;
}

void WiFiPairingPanel::startPairWithCode() {
    // Parse "IP:port" — the address shown on the device's pairing screen
    std::string input(m_ipPortInput);
    // Trim whitespace
    auto trim = [](std::string& s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        if (b == std::string::npos) s.clear();
        else s = s.substr(b, e - b + 1);
    };
    trim(input);

    size_t colon = input.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= input.size()) {
        m_errorMsg = "Invalid pairing address. Expected format \"IP:port\" (e.g. 192.168.1.100:37123).";
        m_state = State::Error;
        return;
    }

    std::string ip = input.substr(0, colon);
    std::string portStr = input.substr(colon + 1);
    for (char c : portStr) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            m_errorMsg = "Invalid pairing port: \"" + portStr + "\".";
            m_state = State::Error;
            return;
        }
    }
    int pairPort = 0;
    try {
        pairPort = std::stoi(portStr);
    } catch (...) {
        m_errorMsg = "Pairing port out of range.";
        m_state = State::Error;
        return;
    }
    if (pairPort <= 0 || pairPort > 65535) {
        m_errorMsg = "Pairing port out of range.";
        m_state = State::Error;
        return;
    }

    std::string code(m_pairCodeInput);
    trim(code);
    if (code.empty()) {
        m_errorMsg = "Please enter the 6-digit pairing code shown on the device.";
        m_state = State::Error;
        return;
    }

    // Optional connect port (shown on the Wireless debugging main screen)
    std::string connPortStr(m_connectPortInput);
    trim(connPortStr);
    m_userProvidedConnectPort = false;
    m_connectPort = 0;
    if (!connPortStr.empty()) {
        bool ok = true;
        for (char c : connPortStr) {
            if (!std::isdigit(static_cast<unsigned char>(c))) { ok = false; break; }
        }
        int cp = 0;
        if (ok) {
            try { cp = std::stoi(connPortStr); }
            catch (...) { cp = 0; }
        }
        if (ok && cp > 0 && cp <= 65535) {
            m_connectPort = cp;
            m_userProvidedConnectPort = true;
        }
    }

    m_pairingIp = ip;
    m_pairingPort = pairPort;

    auto addr = ip + ":" + portStr;
    m_statusMsg = "Pairing with " + addr + "...";
    m_state = State::Pairing;
    runAdbBg({"pair", addr, code});
}

void WiFiPairingPanel::startPairing() {
    auto addr = m_pairingIp + ":" + std::to_string(m_pairingPort);
    m_statusMsg = "Pairing with " + addr + "...";
    runAdbBg({"pair", addr, m_password});
}

void WiFiPairingPanel::startConnecting() {
    auto addr = m_pairingIp + ":" + std::to_string(m_connectPort);
    m_statusMsg = "Connecting to " + addr + "...";
    // adb connect doesn't need special input
    runAdbBg({"connect", addr});
}

// ─── UI rendering ─────────────────────────────────────────────────

bool WiFiPairingPanel::render(DeviceManager& dm) {
    if (!m_open) return false;

    ImGui::SetNextWindowSize(ImVec2(420, 480), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                    ImGui::GetIO().DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool open = true;
    if (!ImGui::Begin("WiFi ADB Pairing", &open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
        ImGui::End();
        if (!open) close();
        return false;
    }
    if (!open) {
        close();
        ImGui::End();
        return false;
    }

    // --- Pairing mode tabs (QR code / pair code) ---
    // During adb operations only the active tab is shown (prevents UI-stack issues).
    bool busy = m_state == State::Pairing || m_state == State::Connecting;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));
    if (ImGui::BeginTabBar("##PairingModeTab")) {
        if (!busy || m_mode == Mode::QR) {
            if (ImGui::BeginTabItem("QR Code")) {
                if (m_mode != Mode::QR) switchMode(Mode::QR);
                ImGui::EndTabItem();
            }
        }
        if (!busy || m_mode == Mode::PairCode) {
            if (ImGui::BeginTabItem("Pair Code")) {
                if (m_mode != Mode::PairCode) switchMode(Mode::PairCode);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    switch (m_state) {
    case State::Ready:
    case State::Discovering: {
        if (m_mode == Mode::PairCode) {
            // --- Pair code form ---
            ImGui::TextWrapped(
                "1. On your Android device: Settings > Developer options > "
                "Wireless debugging > Pair device with pairing code.");
            ImGui::TextWrapped(
                "2. Enter the IP:port and 6-digit code shown on the device.");
            ImGui::Spacing();

            ImGui::Text("Pairing address (IP:port)");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##pair_ip", m_ipPortInput, sizeof(m_ipPortInput),
                             ImGuiInputTextFlags_CharsNoBlank);

            ImGui::Text("Pairing code (6 digits)");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##pair_code", m_pairCodeInput, sizeof(m_pairCodeInput),
                             ImGuiInputTextFlags_CharsDecimal |
                                 ImGuiInputTextFlags_CharsNoBlank);

            ImGui::Text("Connect port (optional)");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##connect_port", m_connectPortInput, sizeof(m_connectPortInput),
                             ImGuiInputTextFlags_CharsDecimal |
                                 ImGuiInputTextFlags_CharsNoBlank);
            ImGui::TextDisabled(
                "Also shown on the Wireless debugging screen. Leave empty to "
                "auto-detect after pairing.");

            ImGui::Spacing();
            if (ImGui::Button("Start Pairing", ImVec2(140, 0))) {
                startPairWithCode();
            }
        } else {
            // --- QR Code display ---
            ImGui::TextWrapped(
                "1. On your Android device, go to Settings > Developer options > "
                "Wireless debugging > Pair device with QR code.");
            ImGui::Spacing();

            if (m_qrTexture) {
                float size = ImGui::GetContentRegionAvail().x - 40;
                if (size > 300) size = 300;
                float x = (ImGui::GetContentRegionAvail().x - size) * 0.5f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
                ImGui::Image((ImTextureID)(uintptr_t)m_qrTexture,
                             ImVec2(size, size));
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextWrapped("2. After scanning, LogCater will auto-discover and pair.");
            ImGui::Spacing();

            if (m_state == State::Discovering) {
                ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f),
                                   "Waiting for device to scan QR code...");
            }

            // Poll mDNS
            float now = static_cast<float>(ImGui::GetTime());
            if (now - m_lastMdnsTime > MDNS_INTERVAL) {
                m_lastMdnsTime = now;
                m_state = State::Discovering;

                if (tryDiscover()) {
                    // Found the pairing service, start pairing
                    m_state = State::Pairing;
                    startPairing();
                }
            }
        }

        break;
    }

    case State::Pairing: {
        ImGui::TextWrapped("Pairing with device...");
        ImGui::Spacing();
        auto col = ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
        ImGui::TextColored(col, "%s", m_statusMsg.c_str());

        if (m_bgDone.load()) {
            bool ok = m_bgSuccess.load();
            std::string output;
            {
                std::lock_guard<std::mutex> lock(m_bgMutex);
                output = m_bgOutput;
            }

            if (!m_open) break; // panel was closed while bg thread ran

            if (ok && output.find("Successfully paired") != std::string::npos) {
                bool haveConnectPort = m_userProvidedConnectPort;
                if (!haveConnectPort) {
                    // Reset retry state and defer to next frame(s)
                    m_connectRetries = 0;
                    m_connectRetryTime = 0.0f;
                    m_bgDone.store(false); // reuse flag space, not used during discovery
                    m_statusMsg = "Paired! Looking for connect service...";
                    m_pairingState = PairingState::WaitingForConnectDiscovery;
                } else {
                    m_state = State::Connecting;
                    startConnecting();
                }
            } else {
                m_errorMsg = "Pairing failed.";
                if (!output.empty())
                    m_errorMsg += "\n" + output;
                m_state = State::Error;
            }
        }

        // Deferred mDNS connect discovery with retries (avoids blocking UI thread)
        if (m_pairingState == PairingState::WaitingForConnectDiscovery) {
            float now = static_cast<float>(ImGui::GetTime());
            if (now - m_connectRetryTime >= CONNECT_RETRY_INTERVAL) {
                m_connectRetryTime = now;
                bool found = tryDiscoverConnect();

                if (found) {
                    m_pairingState = PairingState::Idle;
                    m_state = State::Connecting;
                    startConnecting();
                } else {
                    m_connectRetries++;
                    if (m_connectRetries >= MAX_CONNECT_RETRIES) {
                        // Check if user entered a connect port (may have filled it after clicking Start)
                        std::string connStr(m_connectPortInput);
                        auto trimConn = [](std::string& s) {
                            size_t b = s.find_first_not_of(" \t\r\n");
                            size_t e = s.find_last_not_of(" \t\r\n");
                            if (b == std::string::npos) s.clear();
                            else s = s.substr(b, e - b + 1);
                        };
                        trimConn(connStr);
                        bool hasUserPort = false;
                        int userPort = 0;
                        if (!connStr.empty()) {
                            bool allDigits = true;
                            for (char c : connStr) {
                                if (!std::isdigit(static_cast<unsigned char>(c))) { allDigits = false; break; }
                            }
                            if (allDigits) {
                                try { userPort = std::stoi(connStr); }
                                catch (...) { userPort = 0; }
                                if (userPort > 0 && userPort <= 65535) {
                                    hasUserPort = true;
                                }
                            }
                        }
                        if (hasUserPort) {
                            m_connectPort = userPort;
                            m_pairingState = PairingState::Idle;
                            m_state = State::Connecting;
                            startConnecting();
                        } else {
                            // No auto discovery and no user port — tell user
                            if (m_mode == Mode::PairCode) {
                                m_errorMsg = "Paired successfully, but could not auto-discover "
                                             "the connect port via mDNS.\n\n"
                                             "Please check the Wireless debugging screen on your "
                                             "device for the port next to \"IP address & Port\", "
                                             "enter it in the \"Connect port\" field, then click Retry.";
                            } else {
                                m_errorMsg = "Paired successfully, but could not auto-discover "
                                             "the connect port via mDNS.\n\n"
                                             "Try switching to Pair Code mode and entering the "
                                             "connect port from the Wireless debugging screen.";
                            }
                            m_state = State::Error;
                            m_pairingState = PairingState::Idle;
                        }
                    }
                }
            }

            // Show retry progress
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f),
                               "Looking for connect service... (%d/%d)",
                               m_connectRetries + 1, MAX_CONNECT_RETRIES);
        }

        break;
    }

    case State::Connecting: {
        ImGui::TextWrapped("Connecting to device...");
        ImGui::Spacing();
        auto col = ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
        ImGui::TextColored(col, "%s", m_statusMsg.c_str());

        if (m_bgDone.load()) {
            bool ok = m_bgSuccess.load();
            std::lock_guard<std::mutex> lock(m_bgMutex);
            std::string output = m_bgOutput;

            if (ok && output.find("connected") != std::string::npos) {
                m_state = State::Success;
                m_statusMsg = "Connected!";
                m_connectedSerial = m_pairingIp + ":" + std::to_string(m_connectPort);
                m_justConnected = true;
            } else if (output.find("already connected") != std::string::npos) {
                m_state = State::Success;
                m_statusMsg = "Already connected.";
                m_connectedSerial = m_pairingIp + ":" + std::to_string(m_connectPort);
                m_justConnected = true;
            } else {
                m_errorMsg = "Connection failed.";
                if (!output.empty())
                    m_errorMsg += "\n" + output;
                m_state = State::Error;
            }
        }

        break;
    }

    case State::Success: {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Successfully connected!");

        ImGui::Spacing();
        ImGui::TextWrapped("Device is now available via WiFi:");
        ImGui::BulletText("%s", m_connectedSerial.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            close(); // MainWindow will trigger dm.refreshAsync() via justConnected()
        }

        break;
    }

    case State::Error: {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_errorMsg.c_str());

        // In pair-code mode, if pairing already succeeded but connect failed,
        // let the user edit the connect port and retry connecting directly.
        bool canRetryConnect = (m_mode == Mode::PairCode &&
                                !m_pairingIp.empty() &&
                                m_errorMsg.find("Paired successfully") != std::string::npos);

        if (canRetryConnect) {
            ImGui::Spacing();
            ImGui::Text("Connect port (from Wireless debugging screen)");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##connect_port_err", m_connectPortInput,
                             sizeof(m_connectPortInput),
                             ImGuiInputTextFlags_CharsDecimal |
                                 ImGuiInputTextFlags_CharsNoBlank);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (canRetryConnect) {
            if (ImGui::Button("Connect", ImVec2(100, 0))) {
                // Read connect port from input
                std::string connStr(m_connectPortInput);
                auto trimS = [](std::string& s) {
                    size_t b = s.find_first_not_of(" \t\r\n");
                    size_t e = s.find_last_not_of(" \t\r\n");
                    if (b == std::string::npos) s.clear();
                    else s = s.substr(b, e - b + 1);
                };
                trimS(connStr);
                if (!connStr.empty()) {
                    bool ok = true;
                    int port = 0;
                    for (char c : connStr) {
                        if (!std::isdigit(static_cast<unsigned char>(c))) { ok = false; break; }
                    }
                    if (ok) {
                        try { port = std::stoi(connStr); }
                        catch (...) { port = 0; }
                    }
                    if (ok && port > 0 && port <= 65535) {
                        m_connectPort = port;
                        m_errorMsg = "";
                        m_statusMsg = "";
                        m_state = State::Connecting;
                        startConnecting();
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Back", ImVec2(100, 0))) {
                m_state = State::Ready;
                m_errorMsg = "";
                m_lastMdnsTime = 0.0f;
                m_connectRetries = 0;
            }
        } else {
            if (ImGui::Button("Retry", ImVec2(100, 0))) {
                m_state = State::Ready;
                m_errorMsg = "";
                m_lastMdnsTime = 0.0f;
                m_connectRetries = 0;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(100, 0))) {
            close();
        }

        break;
    }

    case State::Idle:
        break;
    }

    // Always show a close button at the bottom
    if (m_state != State::Success && m_state != State::Pairing &&
        m_state != State::Connecting) {
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Cancel", ImVec2(100, 0)))
            close();
    }

    ImGui::End();
    return true;
}
