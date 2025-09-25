#include "LegacyBridge.h"

#include "memprof.hpp"     // eventos legacy
#include "Protocol.h"      // tópicos y encode
#include "MemoryTracker.h" // ProfilerClient (SDK)

#include <cstdio>
#include <string>

namespace {

    inline const char* kind_str(memprof::EventKind k) noexcept {
        return (k == memprof::EventKind::Alloc) ? "ALLOC" : "FREE";
    }

    // Ojo: esto arma JSON en un buffer de stack para evitar grandes allocs.
    inline int make_json(char* out, int cap, const memprof::Event& ev) noexcept {
        // Campos pueden ser null
        const char* file = ev.file ? ev.file : "";
        const char* type = ev.type ? ev.type : "";
        // NOTA: thread_id/timestamp_ns vienen del legacy
        return std::snprintf(out, cap,
            "{\"kind\":\"%s\",\"ptr\":\"%p\",\"size\":%zu,"
            "\"file\":\"%s\",\"line\":%d,"
            "\"type\":\"%s\",\"is_array\":%d,"
            "\"ts_ns\":%llu,\"thread\":%llu}",
            kind_str(ev.kind), ev.ptr, static_cast<size_t>(ev.size),
            file, ev.line, type, ev.is_array ? 1 : 0,
            (unsigned long long)ev.timestamp_ns,
            (unsigned long long)ev.thread_id
        );
    }

    void legacy_sink_to_broker(const memprof::Event& ev) noexcept {
        // Armar payload JSON en stack
        char json[768];
        int n = make_json(json, sizeof(json), ev);
        if (n <= 0) return;
        try {
            // Publicar al broker en el tópico unificado
            // (puedes cambiar a TOPIC_ALLOCATION/DEALLOCATION si prefieres)
            ProfilerClient::instance().publish(protocol::TOPIC_MEMORY_UPDATE, std::string(json, (size_t)n));
        } catch (...) {
            // Swallow: no romper la app si hay error de red
        }
    }

} // namespace

namespace legacy_bridge {

    void install_socket_sink(const std::string& ip, unsigned short port, const std::string& appId) {
        // Configura el destino del SDK
        ProfilerClient::instance().configure(ip, port, appId);

        // Conectar legacy -> sink
        memprof::set_sink(&legacy_sink_to_broker);
    }

} // namespace legacy_bridge
