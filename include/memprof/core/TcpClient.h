#pragma once
#include <string>
#include <cstddef>

// Cliente TCP muy simple (no Qt) para el runtime
class TcpClient {
public:
    TcpClient();
    ~TcpClient();

    bool connectTo(const char* host, int port);
    bool isConnected() const;
    void close();

    // Envía una línea y añade '\n'
    bool sendLine(const std::string& line);

private:
    int sock_ = -1; // descriptor (SOCKET en Windows convertido a int)
};
