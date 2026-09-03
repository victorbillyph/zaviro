#pragma once

#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <cstdint>

struct NetworkMessage {
    uint16_t type;
    std::string data;
};

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    using MessageCallback = std::function<void(const NetworkMessage&)>;

    bool connect(const std::string& host, int port);
    void disconnect();
    bool isConnected() const { return m_connected; }

    // Tor SOCKS5 proxy support (used to reach .onion servers).
    void setProxy(const std::string& proxyHost, int proxyPort) {
        m_proxyHost = proxyHost;
        m_proxyPort = proxyPort;
        m_useProxy = !proxyHost.empty();
    }
    void setUseProxy(bool use) { m_useProxy = use; }
    bool isUsingProxy() const { return m_useProxy; }

    void send(uint16_t type, const std::string& data);
    void setMessageCallback(MessageCallback cb) { m_callback = cb; }

    void update();

private:
    void receiveLoop();
    bool performSocks5Handshake(int sock, const std::string& host, int port);

    std::thread m_receiveThread;
    std::mutex m_mutex;
    std::queue<NetworkMessage> m_incoming;
    std::queue<NetworkMessage> m_outgoing;
    std::atomic<bool> m_connected{false};
    MessageCallback m_callback;

    void* m_client = nullptr;

    bool m_useProxy = false;
    std::string m_proxyHost;
    int m_proxyPort = 9050;
};
