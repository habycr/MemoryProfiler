#pragma once
#include <string>
#include <cstdint>
#include <mutex>
#include <atomic>
#include "NetUtils.h"

class ProfilerClient {
public:
    static ProfilerClient& instance();

    // Configurar destino
    void configure(const std::string& ip, uint16_t port, const std::string& appId);

    // Enviar payload ya formado (se agrega '\n')
    void publish(const std::string& topic, const std::string& payload);

private:
    ProfilerClient();
    ~ProfilerClient();

    socket_t connectOnce(); // conexión corta por mensaje

    std::mutex mtx_;
    std::string ip_ = "127.0.0.1";
    uint16_t port_ = 5000;
    std::string appId_ = "APP";
};

struct MemoryStats {
    std::atomic<size_t> currentBytes{0};
    std::atomic<size_t> maxBytes{0};
    std::atomic<size_t> activeAllocs{0};
    std::atomic<size_t> totalAllocs{0};
};

MemoryStats& globalMemoryStats();

#ifdef ENABLE_MEMORY_PROFILER
void* operator new(std::size_t sz) noexcept(false);
void operator delete(void* p) noexcept;
#endif
