#pragma once
#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <chrono>

struct MetricsSnapshot {
    double current_mb;      // Memoria actual en MB
    uint64_t active_allocs; // Asignaciones activas (bloques vivos)
    double leak_mb;         // Memory leaks (según umbral) en MB
    double peak_mb;         // Pico histórico en MB
    uint64_t total_allocs;  // Total de ALLOC procesados
    uint64_t timestamp_ms;  // Marca de tiempo del snapshot (epoch ms)

    std::string toJSON() const;
};

class MetricsCalculator {
public:
    MetricsCalculator();

    // Procesamiento de eventos JSON ("ALLOC"/"FREE")
    void processEvent(const std::string& jsonEvent);

    // Métricas actuales (thread-safe)
    MetricsSnapshot getSnapshot() const;

    // Configuración de umbral de "leak" (en ms)
    void setLeakThresholdMs(uint64_t ms);

    // Reinicia estado y contadores
    void reset();

private:
    struct AllocInfo {
        uint64_t size = 0;
        uint64_t timestamp_ns = 0; // ts del evento (ns)
        std::string file;
        int line = 0;
        std::string type;
        bool is_array = false;
    };

    // Estado
    mutable std::mutex mtx_;
    std::unordered_map<std::string, AllocInfo> live_allocs_; // ptr -> info

    std::atomic<uint64_t> current_bytes_{0};
    std::atomic<uint64_t> peak_bytes_{0};
    std::atomic<uint64_t> active_allocs_{0};
    std::atomic<uint64_t> total_allocs_{0};
    std::atomic<uint64_t> leak_threshold_ms_{30000}; // 30s por defecto

    // Helpers privados
    void processAlloc(const std::string& ptr, uint64_t size, uint64_t ts_ns,
                      const std::string& file, int line,
                      const std::string& type, bool is_array);

    void processFree(const std::string& ptr, uint64_t hinted_size);

    uint64_t calculateLeakBytes() const;
    static uint64_t nowMs();
};
