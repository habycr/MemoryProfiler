#include "TcpClient.h"
#include <cstring>
#include <string>
#include <cstdint>   // <-- NECESARIO para uint16_t

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
  static bool g_wsastarted = false;
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR   (-1)
#endif

TcpClient::TcpClient() {
#if defined(_WIN32)
    if (!g_wsastarted) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2,2), &wsaData) == 0) g_wsastarted = true;
    }
#endif
}

TcpClient::~TcpClient() {
    close();
#if defined(_WIN32)
    // no WSACleanup global (se comparte con otros sockets)
#endif
}

bool TcpClient::connectTo(const char* host, int port) {
    close();
    if (!host || !*host || port <= 0) return false;

#if defined(_WIN32)
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
#else
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return false;
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port)); // <-- ya compila

#if defined(_WIN32)
    // inet_pton en Windows: usa InetPtonA
    if (InetPtonA(AF_INET, host, &addr.sin_addr) != 1) {
        // fallback: intenta resolver por nombre
        addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host, nullptr, &hints, &res) == 0 && res) {
            addr.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr;
            freeaddrinfo(res);
        } else {
            ::closesocket(s);
            return false;
        }
    }
#else
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        ::close(s);
        return false;
    }
#endif

    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
#if defined(_WIN32)
        ::closesocket(s);
#else
        ::close(s);
#endif
        return false;
    }

#if defined(_WIN32)
    sock_ = static_cast<int>(s);
#else
    sock_ = s;
#endif
    return true;
}

bool TcpClient::isConnected() const {
    return sock_ != -1;
}

void TcpClient::close() {
    if (sock_ != -1) {
#if defined(_WIN32)
        ::closesocket(static_cast<SOCKET>(sock_));
#else
        ::close(sock_);
#endif
        sock_ = -1;
    }
}

bool TcpClient::sendLine(const std::string& line) {
    if (sock_ == -1) return false;
    std::string buf = line;
    buf.push_back('\n');
    const char* data = buf.c_str();
    size_t left = buf.size();
    while (left > 0) {
#if defined(_WIN32)
        int sent = ::send(static_cast<SOCKET>(sock_), data, static_cast<int>(left), 0);
#else
        ssize_t sent = ::send(sock_, data, left, 0);
#endif
        if (sent <= 0) return false;
        left -= static_cast<size_t>(sent);
        data += sent;
    }
    return true;
}
