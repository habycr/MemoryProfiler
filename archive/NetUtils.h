#pragma once
#include <string>

// Tipos y macros cross-platform
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
#define CLOSESOCK closesocket
#define IS_INVALID(s) ((s)==INVALID_SOCKET)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
using socket_t = int;
#define CLOSESOCK ::close
#define IS_INVALID(s) ((s)<0)
#endif

namespace net {

    // RAII para Winsock (no hace nada en Linux)
    struct WSAInit {
#ifdef _WIN32
        WSAInit();
        ~WSAInit();
#else
        WSAInit() = default;
        ~WSAInit() = default;
#endif
    };

    // Envía todo el buffer (bloqueante). Devuelve true si tuvo éxito.
    bool sendAll(socket_t s, const void* data, size_t len);

    // Lee hasta '\n' (bloqueante). Devuelve true si tuvo éxito.
    bool recvLine(socket_t s, std::string& out);

} // namespace net
