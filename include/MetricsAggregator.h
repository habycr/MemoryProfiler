#pragma once
#include <string>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <vector>

class MetricsAggregator {
public:
    struct BlockInfo {
        std::string ptr;     // clave (texto, ej. "0x7fff...")
        uint64_t    size = 0;
        std::string file;
        int         line = 0;
        std::string type;
        bool        is_array = false;
        uint64_t    ts_ns = 0; // timestamp del ALLOC (ns)
    };

    struct FileStats {
        uint64_t alloc_count = 0;  // total ALLOC (histórico)
        uint64_t alloc_bytes = 0;  // suma de bytes asignados (histórico)
        uint64_t live_count  = 0;  // actualmente vivos
        uint64_t live_bytes  = 0;  // bytes vivos actuales
    };

    struct TimelinePoint {
        uint64_t t_ns = 0;
        uint64_t current_bytes = 0;
        uint64_t leak_bytes = 0;
    };

    struct LeaksKPIs {
        uint64_t total_leak_bytes = 0;
        double   leak_rate = 0.0; // total_leaks / total_allocs
        struct { std::string file; std::string ptr; uint64_t size = 0; } largest;
        struct { std::string file; uint64_t count = 0; uint64_t bytes = 0; } top_file_by_leaks;
    };

    explicit MetricsAggregator(size_t timeline_capacity = 4096);

    // Procesa un evento JSON {"kind":"ALLOC"/"FREE", ...}
    void processEvent(const std::string& json);

    // Lecturas thread-safe para endpoints
    void getMetrics(uint64_t& current_bytes,
                    uint64_t& peak_bytes,
                    uint64_t& active_allocs,
                    uint64_t& total_allocs,
                    uint64_t& leak_bytes) const;

    std::vector<TimelinePoint> getTimeline() const;
    std::vector<BlockInfo> getBlocks() const;
    std::unordered_map<std::string, FileStats> getFileStats() const;
    LeaksKPIs getLeaksKPIs() const;

    // Configuración
    void setLeakThresholdMs(uint64_t ms);
    uint64_t getLeakThresholdMs() const;

private:
    // ====== estado base tipo "contador" ======
    std::atomic<uint64_t> current_bytes_{0};
    std::atomic<uint64_t> peak_bytes_{0};
    std::atomic<uint64_t> active_allocs_{0};
    std::atomic<uint64_t> total_allocs_{0};
    std::atomic<uint64_t> leak_threshold_ms_{30000}; // 30 s

    // ====== estado protegido por mutex ======
    mutable std::mutex mtx_;
    std::unordered_map<std::string, BlockInfo> live_;        // ptr -> info
    std::unordered_map<std::string, FileStats> per_file_;    // file -> stats
    // timeline circular
    const size_t timeline_cap_;
    std::deque<TimelinePoint> timeline_;

    // ====== helpers ======
    static uint64_t now_ns();
    static uint64_t now_ms();

    // JSON helpers (simples, sin dependencias)
    static bool extractString(const std::string& json, const std::string& field, std::string& out);
    static bool extractUint64 (const std::string& json, const std::string& field, uint64_t& out);
    static bool extractInt    (const std::string& json, const std::string& field, int& out);
    static bool extractBool   (const std::string& json, const std::string& field, bool& out);

    // Lógica
    void onAlloc(const std::string& ptr, uint64_t size, uint64_t ts_ns,
                 const std::string& file, int line,
                 const std::string& type, bool is_array);
    void onFree (const std::string& ptr, uint64_t hinted_size);

    uint64_t computeLeakBytes_locked(uint64_t now_ns) const; // requiere mtx_ tomado
    void pushTimelinePoint_locked(uint64_t t_ns, uint64_t cur_b, uint64_t leak_b);

    // Cálculo de KPIs de leaks (requiere mtx_ tomado)
    void computeLeaksKPIs_locked(uint64_t now_ns, LeaksKPIs& out) const;
};
