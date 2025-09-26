#pragma once
#include <string>
#include <atomic>
#include <mutex>
#include "NetUtils.h"

class BrokerClient {
public:
    BrokerClient();
    ~BrokerClient();

    // Configuración
    void configure(const std::string& host, uint16_t port, const std::string& appId);

    // Conexión y suscripción
    bool connect();
    bool subscribe(const std::string& topic);
    void disconnect();

    // Recepción de eventos (bloquea hasta recibir una línea)
    // Retorna el JSON (payload) o string vacío si hay error/desconexión.
    std::string receiveEvent();

    // Estado
    bool isConnected() const;

private:
    socket_t sock_;
    std::string host_;
    uint16_t port_;
    std::string appId_;
    std::atomic<bool> connected_;
    mutable std::mutex mtx_;
    net::WSAInit wsa_; // RAII (no-op en Linux)

    // Helpers internos (NO reimplementan utilidades, solo orquestan)
    bool sendLineLocked_(const std::string& line);
    bool recvLineLocked_(std::string& out);
};
