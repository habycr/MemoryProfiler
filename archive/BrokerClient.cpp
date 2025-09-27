#include "BrokerClient.h"
#include "Protocol.h"
#include "NetUtils.h"

#include <cstring>   // std::strlen
#include <iostream>  // opcional para logs

#ifdef _WIN32
// NetUtils.h ya incluye headers de WinSock
#else
// NetUtils.h ya incluye <sys/socket.h>, <arpa/inet.h>, <unistd.h>
#endif

// Sentinel portable
#ifdef _WIN32
static constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
static constexpr socket_t INVALID_SOCK = (socket_t)-1;
#endif

BrokerClient::BrokerClient()
    : sock_(INVALID_SOCK),
      host_("127.0.0.1"),
      port_(5000),
      appId_("APP"),
      connected_(false) {}

BrokerClient::~BrokerClient() {
    disconnect();
}

void BrokerClient::configure(const std::string& host, uint16_t port, const std::string& appId) {
    std::lock_guard<std::mutex> lk(mtx_);
    host_  = host;
    port_  = port;
    appId_ = appId;
}

bool BrokerClient::connect() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (connected_) return true;

    // Crear socket
    socket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (IS_INVALID(s)) {
        connected_ = false;
        return false;
    }

    // Preparar dirección
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port_);
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        CLOSESOCK(s);
        connected_ = false;
        return false;
    }

    // Conectar
    if (::connect(s, (sockaddr*)&addr, sizeof(addr)) < 0) {
        CLOSESOCK(s);
        connected_ = false;
        return false;
    }

    // Éxito
    sock_ = s;
    connected_ = true;
    return true;
}

bool BrokerClient::sendLineLocked_(const std::string& line) {
    // Asume mtx_ tomado
    if (!connected_) return false;
    return net::sendAll(sock_, line.c_str(), line.size());
}

bool BrokerClient::recvLineLocked_(std::string& out) {
    // Asume mtx_ tomado
    if (!connected_) return false;
    return net::recvLine(sock_, out);
}

bool BrokerClient::subscribe(const std::string& topic) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!connected_) return false;

    // SUBSCRIBE|topic|appId\n  (respetando percent-encoding)
    std::string line = std::string("SUBSCRIBE|")
        + protocol::encode(topic) + "|"
        + protocol::encode(appId_) + "\n";

    if (!sendLineLocked_(line)) {
        connected_ = false;
        CLOSESOCK(sock_);
        sock_ = INVALID_SOCK;
        return false;
    }

    std::string resp;
    if (!recvLineLocked_(resp)) {
        connected_ = false;
        CLOSESOCK(sock_);
        sock_ = INVALID_SOCK;
        return false;
    }

    // Aceptamos "OK\n" o "OK|mensaje\n"
    if (resp.rfind("OK", 0) == 0) {
        return true;
    }

    // Si no fue OK, cerramos por simplicidad (puedes optar por mantener vivo)
    connected_ = false;
    CLOSESOCK(sock_);
    sock_ = INVALID_SOCK;
    return false;
}

std::string BrokerClient::receiveEvent() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!connected_) return {};

    std::string line;
    if (!recvLineLocked_(line)) {
        // Conexión rota / cerrada
        connected_ = false;
        if (!IS_INVALID(sock_)) {
            CLOSESOCK(sock_);
            sock_ = INVALID_SOCK;
        }
        return {};
    }

    // Esperamos líneas del tipo:
    //   OK|<payload_codificado>\n
    //   (o eventualmente "ERROR|..."). Solo retornamos payload si es OK.
    if (line.rfind("OK|", 0) == 0) {
        // Extraer todo lo que viene tras el primer '|'
        // Nota: la línea trae '\n' al final; el decode del Protocol maneja %xx,
        // y el split elimina el '\n' del último token si corresponde.
        // Aquí haremos un split ligero usando helpers existentes.
        // Como queremos el JSON crudo, usamos split() para obtener tokens seguros.
        auto tokens = protocol::split(line);
        // tokens[0] = "OK", tokens[1] = payload (posible JSON codificado)
        if (tokens.size() >= 2) {
            return protocol::decode(tokens[1]);
        } else {
            // Es un "OK" sin payload (no debería pasar para eventos push),
            // devolvemos vacío para no romper al caller.
            return {};
        }
    } else {
        // No es OK: ignoramos y devolvemos vacío.
        return {};
    }
}

void BrokerClient::disconnect() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (connected_) {
        connected_ = false;
    }
    if (!IS_INVALID(sock_)) {
        CLOSESOCK(sock_);
        sock_ = INVALID_SOCK;
    }
}

bool BrokerClient::isConnected() const {
    return connected_.load();
}
