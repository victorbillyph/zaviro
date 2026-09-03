#include "network/client.h"
#include <iostream>
#include <cstring>
#include <vector>

#ifdef _WIN32
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#endif

NetworkClient::NetworkClient() = default;

NetworkClient::~NetworkClient() {
    disconnect();
}

#ifdef _WIN32
DWORD WINAPI NetworkClient::receiveThreadProc(LPVOID param) {
    auto* self = static_cast<NetworkClient*>(param);
    self->receiveLoop();
    return 0;
}
#endif

bool NetworkClient::connect(const std::string& host, int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    std::string dialHost = host;
    int dialPort = port;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    if (m_useProxy) {
        dialHost = m_proxyHost;
        dialPort = m_proxyPort;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(dialPort);
    if (inet_pton(AF_INET, dialHost.c_str(), &addr.sin_addr) != 1) {
        struct hostent* he = gethostbyname(dialHost.c_str());
        if (!he) {
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            return false;
        }
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    }

    if (::connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return false;
    }

    if (m_useProxy) {
        if (!performSocks5Handshake(sock, host, port)) {
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            return false;
        }
    }

    m_client = (void*)(intptr_t)sock;
    m_connected = true;
#ifdef _WIN32
    m_receiveThread = CreateThread(nullptr, 0, receiveThreadProc, this, 0, nullptr);
#else
    m_receiveThread = std::thread(&NetworkClient::receiveLoop, this);
#endif
    return true;
}

bool NetworkClient::performSocks5Handshake(int sock, const std::string& host, int port) {
    const unsigned char greeting[3] = {0x05, 0x01, 0x00};
    if (::send(sock, (const char*)greeting, 3, MSG_NOSIGNAL) != 3) return false;

    unsigned char reply[2];
    if (::recv(sock, (char*)reply, 2, MSG_WAITALL) != 2) return false;
    if (reply[0] != 0x05 || reply[1] != 0x00) return false;

    unsigned char req[4] = {0x05, 0x01, 0x00, 0x03};
    std::vector<unsigned char> reqv;
    reqv.insert(reqv.end(), req, req + 4);
    size_t hostLen = host.size();
    if (hostLen > 255) return false;
    reqv.push_back((unsigned char)hostLen);
    reqv.insert(reqv.end(), host.begin(), host.end());
    reqv.push_back((port >> 8) & 0xFF);
    reqv.push_back(port & 0xFF);

    if (::send(sock, (const char*)reqv.data(), (int)reqv.size(), MSG_NOSIGNAL) != (int)reqv.size()) return false;

    unsigned char hdr[4];
    if (::recv(sock, (char*)hdr, 4, MSG_WAITALL) != 4) return false;
    if (hdr[1] != 0x00) return false;

    unsigned char atyp = hdr[3];
    int addrLen = (atyp == 0x01) ? 4 : (atyp == 0x04) ? 16 : (atyp == 0x03) ? 1 : 0;
    std::vector<unsigned char> rest(addrLen + 2);
    if (::recv(sock, (char*)rest.data(), (int)rest.size(), MSG_WAITALL) != (int)rest.size()) return false;
    if (atyp == 0x03) {
        int domainLen = rest[0];
        std::vector<unsigned char> domain(domainLen + 2);
        if (::recv(sock, (char*)domain.data(), (int)domain.size(), MSG_WAITALL) != (int)domain.size()) return false;
    }

    return true;
}

void NetworkClient::disconnect() {
    m_connected = false;

#ifdef _WIN32
    if (m_receiveThread) {
        WaitForSingleObject(m_receiveThread, 2000);
        CloseHandle(m_receiveThread);
        m_receiveThread = nullptr;
    }
#else
    if (m_receiveThread.joinable()) {
        m_receiveThread.join();
    }
#endif

    if (m_client) {
        int sock = (int)(intptr_t)m_client;
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        m_client = nullptr;
    }
}

void NetworkClient::send(uint16_t type, const std::string& data) {
    if (!m_connected) return;

    {
#ifdef _WIN32
        WinMutex::LockGuard lock(m_mutex);
#else
        std::lock_guard<std::mutex> lock(m_mutex);
#endif
        uint16_t netType = htons(type);
        uint32_t netLen = htonl((uint32_t)data.size());

        int sock = (int)(intptr_t)m_client;
        ::send(sock, (const char*)&netType, 2, MSG_NOSIGNAL);
        ::send(sock, (const char*)&netLen, 4, MSG_NOSIGNAL);
        ::send(sock, data.data(), (int)data.size(), MSG_NOSIGNAL);
    }
}

void NetworkClient::receiveLoop() {
    int sock = (int)(intptr_t)m_client;

    while (m_connected) {
#ifdef _WIN32
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        timeval tv{0, 100000};
        int ret = select(0, &readfds, nullptr, nullptr, &tv);
#else
        pollfd pfd{sock, POLLIN, 0};
        int ret = poll(&pfd, 1, 100);
#endif

        if (ret <= 0) continue;

        uint16_t type;
        uint32_t len;

        if (::recv(sock, (char*)&type, 2, MSG_WAITALL) <= 0) break;
        if (::recv(sock, (char*)&len, 4, MSG_WAITALL) <= 0) break;

        type = ntohs(type);
        len = ntohl(len);

        if (len > 10 * 1024 * 1024) break;

        std::string data(len, '\0');
        if (len > 0 && ::recv(sock, data.data(), len, MSG_WAITALL) <= 0) break;

#ifdef _WIN32
        WinMutex::LockGuard lock(m_mutex);
#else
        std::lock_guard<std::mutex> lock(m_mutex);
#endif
        m_incoming.push({type, std::move(data)});
    }

    m_connected = false;
}

void NetworkClient::update() {
#ifdef _WIN32
    WinMutex::LockGuard lock(m_mutex);
#else
    std::lock_guard<std::mutex> lock(m_mutex);
#endif

    while (!m_incoming.empty()) {
        auto msg = std::move(m_incoming.front());
        m_incoming.pop();

        if (m_callback) {
            m_callback(msg);
        }
    }
}
