#pragma once
#include <string>
#include <mutex>
#include "Protocol.h"
#include "NetUtils.h"

class ProfilerClient {
public:
    static ProfilerClient& instance();

    // Configura destino y appId. Cierra la conexión previa si existía.
    void configure(const std::string& ip, uint16_t port, const std::string& appId);

    // Publica usando una conexión PERSISTENTE (reconecta 1 vez si falla).
    bool publish(const std::string& topic, const std::string& payload);

    // Opcional: timeout de socket (ms) para futuras conexiones.
    void set_timeout_ms(int ms) { timeout_ms_ = ms; }

    ~ProfilerClient();

private:
    ProfilerClient() = default;
    ProfilerClient(const ProfilerClient&) = delete;
    ProfilerClient& operator=(const ProfilerClient&) = delete;

    bool ensure_connected();   // crea y conecta si hace falta
    void close_socket();       // cierra y deja listo para reconectar
    bool set_timeouts();       // aplica SO_RCVTIMEO/SO_SNDTIMEO si se configuró

    std::string ip_;
    uint16_t    port_ = 0;
    std::string appId_;

#ifdef _WIN32
    static constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
    static constexpr socket_t INVALID_SOCK = (socket_t)-1;
#endif
    socket_t    sock_ = INVALID_SOCK;
    int         timeout_ms_ = 0;
    std::mutex  m_;
};
