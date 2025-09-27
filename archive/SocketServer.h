#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <mutex>
#include <optional>
#include "../include/Protocol.h"
#include "../include/NetUtils.h"

// Broker pub/sub con SUBSCRIBE/UNSUBSCRIBE/PUBLISH/RECEIVE
class SocketServer {
public:
    explicit SocketServer(std::string ip, uint16_t port);
    ~SocketServer();

    void start();
    void stop();

private:
    void acceptLoop();
    void clientLoop(socket_t clientSock, std::string clientAddr);
    std::string process(const protocol::Request& req, const std::string& peerId);

    // Pub/sub
    std::unordered_map<std::string, std::unordered_set<std::string>> subscribers_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::deque<std::string>>> queues_;
    std::unordered_map<std::string, socket_t> liveSockets_;

    std::mutex mtx_;

    // Red
    net::WSAInit wsa_;
    std::string ip_;
    uint16_t port_;
    socket_t listenSock_{};
    std::atomic<bool> running_{false};
};
