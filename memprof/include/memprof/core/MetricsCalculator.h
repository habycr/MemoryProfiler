#pragma once

#include "memprof/proto/MetricsSnapshot.h" // usa shims: QString/qulonglong/qlonglong/QVector (o std en backend)
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

class MetricsCalculator {
public:
    MetricsCalculator();

    // Eventos (puntero representado como string; puedes pasar "0x..." o cualquier id)
    void processAlloc(const std::string& ptr,
                      std::uint64_t size,
                      std::uint64_t ts_ns,
                      const std::string& file,
                      int line,
                      const std::string& type,
                      bool is_array);

    void processFree (const std::string& ptr, std::uint64_t hinted_size = 0);

    void processEvent(const std::string& jsonEvent); // opcional

    void setLeakThresholdMs(std::uint64_t ms);

    MetricsSnapshot getSnapshot() const;

    void reset();

    // Serialización simple del snapshot (en backend devolverá JSON textual)
    friend QString toJSON(const MetricsSnapshot& s);

private:
    struct AllocInfo {
        std::uint64_t size = 0;
        std::uint64_t ts_ns = 0;    // timestamp de alloc
        std::string   file;
        int           line = 0;
        std::string   type;
        bool          is_array = false;
    };

    // Contadores principales
    std::atomic<std::uint64_t> current_bytes_{0};
    std::atomic<std::uint64_t> peak_bytes_{0};
    std::atomic<std::uint64_t> active_allocs_{0};
    std::atomic<std::uint64_t> total_allocs_{0};

    // Umbral para considerar fugas por antigüedad
    std::atomic<std::uint64_t> leak_threshold_ms_{3000}; // 3s por defecto

    // Tasas (EWMA)
    mutable std::mutex rate_mtx_;
    double       alloc_rate_{0.0};
    double       free_rate_{0.0};
    std::uint64_t last_alloc_ts_ns_{0};
    std::uint64_t last_free_ts_ns_{0};

    // Bloques vivos
    mutable std::mutex mtx_;
    std::unordered_map<std::string, AllocInfo> live_allocs_;

    // Helpers
    static std::uint64_t nowMs();
    std::uint64_t calculateLeakBytes() const;
};
