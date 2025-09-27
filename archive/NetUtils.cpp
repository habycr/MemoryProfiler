#include "NetUtils.h"
#include <iostream>

#ifdef _WIN32
  #pragma comment(lib,"ws2_32.lib")
#endif

namespace net {

#ifdef _WIN32
    WSAInit::WSAInit() {
        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed\n";
        }
    }
    WSAInit::~WSAInit() {
        WSACleanup();
    }
#endif

    bool sendAll(socket_t s, const void* data, size_t len) {
        const char* p = static_cast<const char*>(data);
        size_t total = 0;
        while (total < len) {
#ifdef _WIN32
            int sent = ::send(s, p + total, static_cast<int>(len - total), 0);
#else
            ssize_t sent = ::send(s, p + total, len - total, 0);
#endif
            if (sent <= 0) return false;
            total += static_cast<size_t>(sent);
        }
        return true;
    }

    bool recvLine(socket_t s, std::string& out) {
        out.clear();
        char c;
        while (true) {
#ifdef _WIN32
            int n = ::recv(s, &c, 1, 0);
#else
            ssize_t n = ::recv(s, &c, 1, 0);
#endif
            if (n <= 0) return false;
            out.push_back(c);
            if (c == '\n') break;
            if (out.size() > 65536) return false; // límite simple
        }
        return true;
    }

} // namespace net
