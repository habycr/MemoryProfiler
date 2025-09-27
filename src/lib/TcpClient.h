#pragma once
#include <string>

class TcpClient {
public:
    TcpClient();
    ~TcpClient();

    bool connectTo(const char* host, int port);
    bool sendLine(const std::string& line); // agrega '\n' si no está
    void close();

    // Wrappers públicos para envío de datos
    bool send(const std::string& s) { return sendAll(s.c_str(), s.size()); }
    bool send(const char* data, size_t len) { return sendAll(data, len); }


    bool isConnected() const { return connected_; }

private:
#ifdef _WIN32
    using socket_t = uintptr_t;
#else
    using socket_t = int;
#endif
    socket_t sock_;
    bool connected_;
    bool wsa_ready_;

    bool sendAll(const char* data, size_t len);
};
