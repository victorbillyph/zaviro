#pragma once

#include <string>
#include <functional>
#include <queue>
#include <atomic>
#include <cstdint>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <thread>
#include <mutex>
#endif

struct NetworkMessage {
    uint16_t type;
    std::string data;
};

#ifdef _WIN32
class WinMutex {
    CRITICAL_SECTION cs;
public:
    WinMutex() { InitializeCriticalSection(&cs); }
    ~WinMutex() { DeleteCriticalSection(&cs); }
    void lock() { EnterCriticalSection(&cs); }
    void unlock() { LeaveCriticalSection(&cs); }

    class LockGuard {
        WinMutex& m;
    public:
        explicit LockGuard(WinMutex& mutex) : m(mutex) { m.lock(); }
        ~LockGuard() { m.unlock(); }
        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(const LockGuard&) = delete;
    };
};
#endif

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
#ifdef _WIN32
    static DWORD WINAPI receiveThreadProc(LPVOID param);
#endif

#ifdef _WIN32
    HANDLE m_receiveThread = nullptr;
    WinMutex m_mutex;
#else
    std::thread m_receiveThread;
    std::mutex m_mutex;
#endif

    std::queue<NetworkMessage> m_incoming;
    std::queue<NetworkMessage> m_outgoing;
    std::atomic<bool> m_connected{false};
    MessageCallback m_callback;

    void* m_client = nullptr;

    bool m_useProxy = false;
    std::string m_proxyHost;
    int m_proxyPort = 9050;
};
