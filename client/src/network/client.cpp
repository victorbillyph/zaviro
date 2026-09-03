#include "network/client.h"
#include <iostream>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
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

bool NetworkClient::connect(const std::string& host, int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    // Determine what to actually dial.
    // When a SOCKS5 proxy is configured (Tor), we connect to the proxy and
    // ask it to reach host:port (supports .onion domains). Otherwise direct.
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
        // try resolving via getaddrinfo
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

    // If using a proxy, perform the SOCKS5 CONNECT handshake to the real target.
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
    m_receiveThread = std::thread(&NetworkClient::receiveLoop, this);
    return true;
}

bool NetworkClient::performSocks5Handshake(int sock, const std::string& host, int port) {
    // Greeting: ver=5, nmethods=1, method=0x00 (no auth)
    const unsigned char greeting[3] = {0x05, 0x01, 0x00};
    if (::send(sock, (const char*)greeting, 3, MSG_NOSIGNAL) != 3) return false;

    unsigned char reply[2];
    if (::recv(sock, (char*)reply, 2, MSG_WAITALL) != 2) return false;
    if (reply[0] != 0x05 || reply[1] != 0x00) return false; // no acceptable auth

    // CONNECT request with domain name (ATYP=0x03)
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
    if (hdr[1] != 0x00) return false; // connect failed

    // Parse remaining address based on ATYP
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
