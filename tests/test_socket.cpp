#include "Protocol.h"
#include "MemoryTracker.h"
#include "NetUtils.h"
#include <iostream>

static bool subscribe(const std::string& ip, uint16_t port,
                      const std::string& topic, const std::string& appId) {
    net::WSAInit wsa;  // seguro en Win, no-op en Linux

    socket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) return false;
#else
    if (s < 0) return false;
#endif
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &a.sin_addr);
    if (::connect(s, (sockaddr*)&a, sizeof(a)) < 0) { CLOSESOCK(s); return false; }

    std::string line = "SUBSCRIBE|" + protocol::encode(topic) + "|" + protocol::encode(appId) + "\n";
    net::sendAll(s, line.c_str(), line.size());
    std::string resp;
    if (!net::recvLine(s, resp)) { CLOSESOCK(s); return false; }
    std::cout << "SUBSCRIBE resp: " << resp;
    CLOSESOCK(s);
    return resp.rfind("OK", 0) == 0;
}

int main() {
    try {
        std::string ip = "127.0.0.1";
        uint16_t port = 5000;

        bool ok = subscribe(ip, port, protocol::TOPIC_MEMORY_UPDATE, "GUI-1");
        if (!ok) { std::cerr << "Falló SUBSCRIBE\n"; return 1; }

        ProfilerClient::instance().configure(ip, port, "APP-1");

        std::string payload = std::string(protocol::TOPIC_MEMORY_UPDATE) + "|"
            + "123456789" + "|256.5|42|512.7|123|APP-1";
        ProfilerClient::instance().publish(protocol::TOPIC_MEMORY_UPDATE, payload);

        std::cout << "OK test\n";
        return 0;
    } catch (...) {
        return 1;
    }
}
