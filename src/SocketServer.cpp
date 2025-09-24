#include "SocketServer.h"
#include "NetUtils.h"
#include <iostream>
#include <cstring>
#include <chrono>

#ifdef _WIN32
  #pragma comment(lib,"ws2_32.lib")
#endif

SocketServer::SocketServer(std::string ip, uint16_t port)
: ip_(std::move(ip)), port_(port) {
#ifdef _WIN32
    listenSock_ = INVALID_SOCKET;
#else
    listenSock_ = -1;
#endif
}

SocketServer::~SocketServer() {
    stop();
}

void SocketServer::start() {
    if (running_.exchange(true)) return;

    listenSock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (IS_INVALID(listenSock_)) {
        running_ = false;
        throw std::runtime_error("No se pudo crear el socket de escucha");
    }

    int yes = 1;
#ifdef _WIN32
    setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#else
    setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (::inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr) != 1) {
        CLOSESOCK(listenSock_);
        running_ = false;
        throw std::runtime_error("IP inválida: " + ip_);
    }
    if (::bind(listenSock_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        CLOSESOCK(listenSock_);
        running_ = false;
        throw std::runtime_error("bind() falló");
    }
    if (::listen(listenSock_, 16) < 0) {
        CLOSESOCK(listenSock_);
        running_ = false;
        throw std::runtime_error("listen() falló");
    }

    std::thread(&SocketServer::acceptLoop, this).detach();
    std::cout << "Broker escuchando en " << ip_ << ":" << port_ << std::endl;
}

void SocketServer::stop() {
    if (!running_.exchange(false)) return;
    if (!IS_INVALID(listenSock_)) {
        CLOSESOCK(listenSock_);
#ifdef _WIN32
        listenSock_ = INVALID_SOCKET;
#else
        listenSock_ = -1;
#endif
    }
}

void SocketServer::acceptLoop() {
    while (running_) {
        sockaddr_in cli{};
#ifdef _WIN32
        int len = sizeof(cli);
#else
        socklen_t len = sizeof(cli);
#endif
        socket_t cs = ::accept(listenSock_, (sockaddr*)&cli, &len);
        if (IS_INVALID(cs)) {
            if (!running_) break;
            continue;
        }
        char buf[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &cli.sin_addr, buf, sizeof(buf));
        std::string peer = buf;

        // LOG: cliente aceptado
        std::cout << "[ACCEPT] " << peer << std::endl;

        std::thread(&SocketServer::clientLoop, this, cs, peer).detach();
    }
}

void SocketServer::clientLoop(socket_t clientSock, std::string clientAddr) {
    std::string line;
    std::string peerId = clientAddr; // se actualizará si llega un appId en SUBSCRIBE

    while (running_) {
        if (!net::recvLine(clientSock, line)) {
            // LOG: desconexión
            std::cout << "[DISCONNECT] " << clientAddr << std::endl;
            break;
        }

        // LOG: línea recibida (incluye '\n')
        std::cout << "[RX] " << clientAddr << " " << line;

        auto reqOpt = protocol::parseLine(line);
        if (!reqOpt) {
            auto msg = protocol::error("Petición inválida");
            std::cout << "[PARSE-ERR] " << clientAddr << " -> " << msg; // log del error enviado
            net::sendAll(clientSock, msg.c_str(), msg.size());
            continue;
        }

        auto resp = process(*reqOpt, peerId);

        // LOG: respuesta enviada
        std::cout << "[TX] " << clientAddr << " " << resp;

        net::sendAll(clientSock, resp.c_str(), resp.size());

        // Mantener socket vivo asociado a appId para push
        if (reqOpt->command == "SUBSCRIBE" && reqOpt->args.size() >= 2) {
            std::lock_guard<std::mutex> lock(mtx_);
            peerId = reqOpt->args[1];
            liveSockets_[peerId] = clientSock;
        } else if (reqOpt->command == "UNSUBSCRIBE" && reqOpt->args.size() >= 2) {
            std::lock_guard<std::mutex> lock(mtx_);
            liveSockets_.erase(reqOpt->args[1]);
        }
    }

    // limpiar socket saliente en mapa de sockets vivos
    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto it = liveSockets_.begin(); it != liveSockets_.end();) {
            if (it->second == clientSock) it = liveSockets_.erase(it);
            else ++it;
        }
    }
    CLOSESOCK(clientSock);
}

std::string SocketServer::process(const protocol::Request& req, const std::string& peerId) {
    try {
        if (req.command == "SUBSCRIBE") {
            // SUBSCRIBE|topic|appId
            if (req.args.size() < 2) return protocol::error("Faltan parámetros para SUBSCRIBE");
            const auto& topic = req.args[0];
            const auto& appId = req.args[1];
            std::lock_guard<std::mutex> lock(mtx_);
            subscribers_[topic].insert(appId);
            queues_[topic];
            queues_[topic][appId];

            // LOG detallado
            std::cout << "[SUBSCRIBE] topic=" << topic << " appId=" << appId << std::endl;

            return protocol::ok("Suscripción exitosa");
        }

        if (req.command == "UNSUBSCRIBE") {
            // UNSUBSCRIBE|topic|appId
            if (req.args.size() < 2) return protocol::error("Faltan parámetros para UNSUBSCRIBE");
            const auto& topic = req.args[0];
            const auto& appId = req.args[1];
            std::lock_guard<std::mutex> lock(mtx_);
            if (!subscribers_.count(topic) || !subscribers_[topic].count(appId)) {
                return protocol::error("No está suscrito al tema: " + topic);
            }
            subscribers_[topic].erase(appId);
            if (queues_.count(topic)) queues_[topic].erase(appId);
            liveSockets_.erase(appId);

            // LOG detallado
            std::cout << "[UNSUBSCRIBE] topic=" << topic << " appId=" << appId << std::endl;

            return protocol::ok("Desuscripción exitosa");
        }

        if (req.command == "PUBLISH") {
            // PUBLISH|topic|payload|appId
            if (req.args.size() < 3) return protocol::error("Faltan parámetros para PUBLISH");
            const auto& topic   = req.args[0];
            const auto& payload = req.args[1];
            const auto& appId   = req.args[2];

            size_t pushed = 0;
            size_t queued_for = 0;

            std::lock_guard<std::mutex> lock(mtx_);
            if (!subscribers_.count(topic) || subscribers_[topic].empty()) {
                // LOG sin suscriptores
                std::cout << "[PUBLISH] topic=" << topic << " from=" << appId
                          << " queued_for=0 pushed=0 (sin suscriptores)" << std::endl;
                return protocol::error("No hay suscriptores para el tema: " + topic);
            }

            queued_for = subscribers_[topic].size();
            for (const auto& sub : subscribers_[topic]) {
                queues_[topic][sub].push_back(payload);
                if (auto itSock = liveSockets_.find(sub); itSock != liveSockets_.end()) {
                    std::string pushLine = "OK|" + protocol::encode(payload) + "\n";
                    net::sendAll(itSock->second, pushLine.c_str(), pushLine.size());
                    ++pushed;
                }
            }

            // LOG detallado
            std::cout << "[PUBLISH] topic=" << topic
                      << " from=" << appId
                      << " queued_for=" << queued_for
                      << " pushed=" << pushed << std::endl;

            return protocol::ok("Mensaje publicado a " + std::to_string(pushed) + " suscriptores");
        }

        if (req.command == "RECEIVE") {
            // RECEIVE|topic|appId
            if (req.args.size() < 2) return protocol::error("Faltan parámetros para RECEIVE");
            const auto& topic = req.args[0];
            const auto& appId = req.args[1];
            std::lock_guard<std::mutex> lock(mtx_);
            if (!subscribers_.count(topic) || !subscribers_[topic].count(appId)) {
                return protocol::error("No está suscrito al tema: " + topic);
            }
            auto& q = queues_[topic][appId];
            if (q.empty()) return protocol::error("No hay mensajes en la cola");
            std::string msg = q.front(); q.pop_front();

            // LOG detallado
            std::cout << "[RECEIVE] topic=" << topic
                      << " appId=" << appId << " delivered_one" << std::endl;

            return protocol::ok(msg);
        }

        // Atajos de profiler (tratan la línea como evento y la publican al topic homónimo)
        if (req.command == protocol::TOPIC_MEMORY_UPDATE ||
            req.command == protocol::TOPIC_ALLOCATION    ||
            req.command == protocol::TOPIC_DEALLOCATION  ||
            req.command == protocol::TOPIC_LEAK_DETECTED ||
            req.command == protocol::TOPIC_FILE_STATS) {

            if (req.args.empty()) return protocol::error("Faltan parámetros");
            std::string payload;
            payload.reserve(256);
            payload += req.command;
            for (auto& a : req.args) { payload += '|'; payload += a; }

            std::string appId = req.args.back();
            if (appId.size() < 8) appId = peerId;

            size_t pushed = 0;
            size_t subs = 0;

            std::lock_guard<std::mutex> lock(mtx_);
            const std::string topic = req.command;
            subs = subscribers_[topic].size(); // 0 si nadie se suscribió aún
            for (const auto& sub : subscribers_[topic]) {
                queues_[topic][sub].push_back(payload);
                if (auto itSock = liveSockets_.find(sub); itSock != liveSockets_.end()) {
                    std::string pushLine = "OK|" + protocol::encode(payload) + "\n";
                    net::sendAll(itSock->second, pushLine.c_str(), pushLine.size());
                    ++pushed;
                }
            }

            // LOG de evento de profiler
            std::cout << "[EVENT] " << req.command
                      << " subs=" << subs
                      << " pushed=" << pushed << std::endl;

            return protocol::ok("Evento " + req.command + " publicado a " + std::to_string(pushed));
        }

        return protocol::error("Comando desconocido: " + req.command);
    } catch (const std::exception& ex) {
        return protocol::error(std::string("Error interno: ") + ex.what());
    }
}
