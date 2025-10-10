#pragma once
#include <string>
#include <unordered_map>
#include <deque>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstdint>

class MetricsAggregator {
public:
    struct BlockInfo {
        std::string ptr;     // dirección como string/hex
        uint64_t    size = 0;
        std::string file;
        int         line = 0;
        std::string type;
        bool        is_array = false;
        uint64_t    ts_ns = 0; // timestamp de alloc
    };

    struct FileStats {
        uint64_t alloc_count = 0;  // nº total de allocs vistos
        uint64_t alloc_bytes = 0;  // bytes totales asignados (histórico)
        uint64_t live_count  = 0;  // allocs vivos
        uint64_t live_bytes  = 0;  // bytes vivos
    };

    struct TimelinePoint {
        uint64_t t_ns = 0;
        uint64_t cur_bytes = 0;
        uint64_t leak_bytes = 0;
    };

    struct LeaksKPIs {
        uint64_t total_leak_bytes = 0;
        double   leak_rate = 0.0;
        struct { std::string file, ptr; uint64_t size = 0; } largest;
        struct { std::string file; uint64_t count = 0, bytes = 0; } top_file_by_leaks;
    };

public:
    explicit MetricsAggregator(size_t timeline_capacity = 4096);

    // Ingesta de eventos (desde tus hooks/new/delete)
    void onAlloc(const std::string& ptr, uint64_t size, uint64_t ts_ns,
                 const std::string& file, int line,
                 const std::string& type, bool is_array);
    void onFree (const std::string& ptr, uint64_t hinted_size);

    // Ingesta “texto json” (si envías eventos en JSON)
    void processEvent(const std::string& json);

    // Consulta de métricas agregadas
    void getMetrics(uint64_t& current_bytes,
                    uint64_t& peak_bytes,
                    uint64_t& active_allocs,
                    uint64_t& total_allocs,
                    uint64_t& leak_bytes) const;

    std::vector<TimelinePoint> getTimeline() const;
    std::vector<BlockInfo>     getBlocks()   const;   // ← UNA sola declaración
    std::unordered_map<std::string, FileStats> getFileStats() const;
    LeaksKPIs getLeaksKPIs() const;

    void     setLeakThresholdMs(uint64_t ms);
    uint64_t getLeakThresholdMs() const;

    static uint64_t now_ns();
    static uint64_t now_ms();

private:
    // JSON helpers (mínimos)
    static bool extractString(const std::string& json, const std::string& field, std::string& out);
    static bool extractBool  (const std::string& json, const std::string& field, bool& out);
    static bool extractUint64(const std::string& json, const std::string& field, uint64_t& out);
    static bool extractInt   (const std::string& json, const std::string& field, int& out);

    uint64_t computeLeakBytes_locked(uint64_t now_ns_val) const;
    void     computeLeaksKPIs_locked(uint64_t now_ns_val, LeaksKPIs& out) const;
    void     pushTimelinePoint_locked(uint64_t t_ns, uint64_t cur_b, uint64_t leak_b);

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, BlockInfo>  live_;
    std::unordered_map<std::string, FileStats>  per_file_;
    std::deque<TimelinePoint>                   timeline_;
    size_t                                      timeline_cap_;

    std::atomic<uint64_t> total_allocs_{0};
    std::atomic<uint64_t> active_allocs_{0};
    std::atomic<uint64_t> current_bytes_{0};
    std::atomic<uint64_t> peak_bytes_{0};
    std::atomic<uint64_t> leak_threshold_ms_{3000}; // p.ej. 3s
};
