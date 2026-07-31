#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Forward declarations
class DeviceManager;

/// WiFi ADB pairing panel — Android 11+ wireless debugging.
/// Two pairing modes:
///  - QR code: generates a QR code, polls mDNS, auto-pairs and connects.
///  - Pair code: user enters the IP:port + 6-digit code shown on the device.
class WiFiPairingPanel {
public:
    WiFiPairingPanel();
    ~WiFiPairingPanel();

    /// Render the pairing popup. Returns true while the popup is open.
    bool render(DeviceManager& dm);

    /// Open/close the panel.
    void open();
    void close();
    bool isOpen() const { return m_open; }

    /// Polled by MainWindow to trigger device refresh after successful pairing.
    bool justConnected() const;
    void clearConnectedFlag();

private:
    enum class State {
        Idle,           // Not shown / initial
        Ready,          // Waiting for input / QR scan
        Discovering,    // Polling adb mdns services
        Pairing,        // Running adb pair
        Connecting,     // Running adb connect
        Success,        // Connected
        Error           // Failed with message
    };

    enum class Mode {
        QR,         // Scan-with-device QR code flow
        PairCode    // Manual 6-digit pairing code flow
    };

    enum class PairingState {
        Idle,
        WaitingForConnectDiscovery  // paired, need to find connect port via mDNS
    };

    // mDNS connect-port retry (after pairing succeeds)
    int m_connectRetries = 0;
    float m_connectRetryTime = 0.0f;
    static constexpr int MAX_CONNECT_RETRIES = 3;
    static constexpr float CONNECT_RETRY_INTERVAL = 1.5f;

    bool m_open = false;
    State m_state = State::Idle;
    Mode m_mode = Mode::QR;
    PairingState m_pairingState = PairingState::Idle;
    std::string m_statusMsg;
    std::string m_errorMsg;

    // QR code data
    std::string m_serviceName;    // "studio-XXXXXXXXXX"
    std::string m_password;       // 10-char pairing password
    std::string m_qrString;       // "WIFI:T:ADB;S:...;P:...;;"

    // QR texture
    unsigned int m_qrTexture = 0;
    int m_qrTexWidth = 0;
    int m_qrTexHeight = 0;

    /// Generate random pairing credentials.
    void generateCredentials();

    /// Create/deallocate QR texture.
    void buildQrTexture();
    void destroyQrTexture();

    // Pair-code mode input (device shows these on the "Pair with pairing code" screen)
    char m_ipPortInput[64] = {};        // "192.168.1.100:37123"
    char m_pairCodeInput[16] = {};      // 6-digit code
    char m_connectPortInput[16] = {};   // optional connect port (Wireless debugging screen)
    bool m_userProvidedConnectPort = false;

    /// Start "adb pair" with the user-entered pairing code.
    void startPairWithCode();

    /// Switch pairing mode, resetting transient state (no-op while busy).
    void switchMode(Mode mode);

    // mDNS discovery
    float m_lastMdnsTime = 0.0f;
    static constexpr float MDNS_INTERVAL = 2.0f;

    // Pairing worker
    std::string m_pairingIp;
    int m_pairingPort = 0;
    int m_connectPort = 0;

    void startPairing();
    void startConnecting();

    // Background thread for adb commands
    std::thread m_bgThread;
    std::atomic<bool> m_bgDone{false};
    std::string m_bgOutput;
    std::atomic<bool> m_bgSuccess{false};
    mutable std::mutex m_bgMutex;

    /// Run adb command on background thread.
    void runAdbBg(std::vector<std::string> args, std::string stdinData = "");

    // Try to discover the pairing service via "adb mdns services"
    bool tryDiscover();
    bool tryDiscoverConnect();

    // Connected flag (set after successful connect)
    std::string m_connectedSerial; // IP:port that was connected
    bool m_justConnected = false;
};
