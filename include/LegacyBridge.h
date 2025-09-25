#pragma once
#include <string>

namespace legacy_bridge {

    // Configura el destino (broker) e instala el sink de legacy.
    // Llama a esto al inicio de la app que quieras perfilar (NO el servidor).
    void install_socket_sink(const std::string& ip, unsigned short port, const std::string& appId);

} // namespace legacy_bridge
