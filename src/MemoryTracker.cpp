#include "MemoryTracker.h"
#include <iostream>
#include <cstring>

ProfilerClient::~ProfilerClient() { close_socket(); }

ProfilerClient& ProfilerClient::instance() {
    static ProfilerClient s;
    return s;
}

void ProfilerClient::configure(const std::string& ip, uint16_t port, const std::string& appId) {
    std::lock_guard<std::mutex> lk(m_);
    ip_ = ip; port_ = port; appId_ = appId;
    close_socket(); // forzar nueva conexión con la nueva config
}

bool ProfilerClient::publish(const std::string& topic, const std::string& payload) {
    std::lock_guard<std::mutex> lk(m_);
    if (!ensure_connected()) return false;

    std::string line = "PUBLISH|" + protocol::encode(topic)
                     + "|" + protocol::encode(payload);
    if (!appId_.empty()) line += "|" + protocol::encode(appId_);
    line += "\n";

    if (!net::sendAll(sock_, line.c_str(), line.size())) {
        // Reintento 1: reconectar y volver a enviar
        close_socket();
        if (!ensure_connected()) return false;
        if (!net::sendAll(sock_, line.c_str(), line.size())) {
            close_socket();
            return false;
        }
    }
    return true;
}

bool ProfilerClient::ensure_connected() {
    if (!IS_INVALID(sock_)) return true;
    if (ip_.empty() || port_ == 0) return false;

    sock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (IS_INVALID(sock_)) { sock_ = INVALID_SOCK; return false; }

    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port_);
    if (::inet_pton(AF_INET, ip_.c_str(), &a.sin_addr) != 1) {
        close_socket(); return false;
    }
    if (::connect(sock_, (sockaddr*)&a, sizeof(a)) < 0) {
        close_socket(); return false;
    }
    (void)set_timeouts();
    return true;
}

void ProfilerClient::close_socket() {
    if (!IS_INVALID(sock_)) { CLOSESOCK(sock_); }
#ifdef _WIN32
    sock_ = INVALID_SOCKET;
#else
    sock_ = (socket_t)-1;
#endif
}

bool ProfilerClient::set_timeouts() {
    if (timeout_ms_ <= 0 || IS_INVALID(sock_)) return true;
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(timeout_ms_);
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#else
    timeval tv{};
    tv.tv_sec  = timeout_ms_ / 1000;
    tv.tv_usec = (timeout_ms_ % 1000) * 1000;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    return true;
}
