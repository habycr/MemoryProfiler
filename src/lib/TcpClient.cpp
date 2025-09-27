#include "TcpClient.h"
#include <cstring>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netdb.h>
  #include <unistd.h>
#endif

TcpClient::TcpClient() : sock_(static_cast<socket_t>(-1)), connected_(false), wsa_ready_(false) {
#ifdef _WIN32
    WSADATA wsa; if (WSAStartup(MAKEWORD(2,2), &wsa)==0) wsa_ready_=true;
#else
    wsa_ready_ = true;
#endif
}
TcpClient::~TcpClient() { close();
#ifdef _WIN32
    if (wsa_ready_) WSACleanup();
#endif
}

bool TcpClient::connectTo(const char* host, int port) {
    close();
    if (!wsa_ready_) return false;

    char portStr[16];
    std::snprintf(portStr, sizeof(portStr), "%d", port);

    addrinfo hints{}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, portStr, &hints, &res) != 0) return false;

    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        socket_t s =
#ifdef _WIN32
            reinterpret_cast<socket_t>(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
#else
            ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
#endif
        if (s == static_cast<socket_t>(-1)) continue;

        if (::connect((int)s, ai->ai_addr, (int)ai->ai_addrlen) == 0) {
            sock_ = s; connected_ = true; freeaddrinfo(res); return true;
        }
#ifdef _WIN32
        ::closesocket((SOCKET)s);
#else
        ::close(s);
#endif
    }
    freeaddrinfo(res);
    return false;
}

bool TcpClient::sendAll(const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int n = ::send((SOCKET)sock_, data + sent, (int)(len - sent), 0);
#else
        int n = ::send(sock_, data + sent, (int)(len - sent), 0);
#endif
        if (n <= 0) { connected_ = false; return false; }
        sent += (size_t)n;
    }
    return true;
}

bool TcpClient::sendLine(const std::string& line) {
    if (!connected_) return false;
    if (!sendAll(line.c_str(), line.size())) return false;
    if (line.empty() || line.back() != '\n') {
        const char nl = '\n';
        if (!sendAll(&nl, 1)) return false;
    }
    return true;
}

void TcpClient::close() {
    if (!connected_) return;
#ifdef _WIN32
    ::shutdown((SOCKET)sock_, SD_BOTH);
    ::closesocket((SOCKET)sock_);
#else
    ::shutdown(sock_, SHUT_RDWR);
    ::close(sock_);
#endif
    connected_ = false;
    sock_ = static_cast<socket_t>(-1);
}
