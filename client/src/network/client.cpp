#include "network/client.h"
#include <iostream>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#endif

NetworkClient::NetworkClient() = default;

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connect(const std::string& host, int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (::connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return false;
    }

    m_client = (void*)(intptr_t)sock;
    m_connected = true;
    m_receiveThread = std::thread(&NetworkClient::receiveLoop, this);
    return true;
}

void NetworkClient::disconnect() {
    m_connected = false;

    if (m_receiveThread.joinable()) {
        m_receiveThread.join();
    }

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

    std::lock_guard<std::mutex> lock(m_mutex);
    uint16_t netType = htons(type);
    uint32_t netLen = htonl((uint32_t)data.size());

    int sock = (int)(intptr_t)m_client;
    ::send(sock, &netType, 2, MSG_NOSIGNAL);
    ::send(sock, &netLen, 4, MSG_NOSIGNAL);
    ::send(sock, data.data(), data.size(), MSG_NOSIGNAL);
}

void NetworkClient::receiveLoop() {
    int sock = (int)(intptr_t)m_client;

    while (m_connected) {
        pollfd pfd{sock, POLLIN, 0};
        int ret = poll(&pfd, 1, 100);

        if (ret <= 0) continue;

        uint16_t type;
        uint32_t len;

        if (recv(sock, &type, 2, MSG_WAITALL) <= 0) break;
        if (recv(sock, &len, 4, MSG_WAITALL) <= 0) break;

        type = ntohs(type);
        len = ntohl(len);

        if (len > 10 * 1024 * 1024) break;

        std::string data(len, '\0');
        if (len > 0 && recv(sock, data.data(), len, MSG_WAITALL) <= 0) break;

        std::lock_guard<std::mutex> lock(m_mutex);
        m_incoming.push({type, std::move(data)});
    }

    m_connected = false;
}

void NetworkClient::update() {
    std::lock_guard<std::mutex> lock(m_mutex);

    while (!m_incoming.empty()) {
        auto msg = std::move(m_incoming.front());
        m_incoming.pop();

        if (m_callback) {
            m_callback(msg);
        }
    }
}
