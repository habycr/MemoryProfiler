#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <numeric>

#include "MetricsAggregator.h"
#include "src/lib/TcpClient.h"
#include "src/lib/memprof_api.h"

using steady_clock_t = std::chrono::steady_clock;

// ---------- estado global ----------
static MetricsAggregator g_agg;
static std::atomic<bool> g_running{false};
static std::thread       g_tx_thread;
static std::mutex        g_cfg_mtx;
static std::string       g_host = "127.0.0.1";
static int               g_port = 7070;
static uint64_t          g_start_ms = 0;

// ---------- helpers ----------
static inline uint64_t now_ms() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               steady_clock_t::now().time_since_epoch())
        .count();
}

static inline uint64_t now_ns() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               steady_clock_t::now().time_since_epoch())
        .count();
}

static inline std::string json_escape(const std::string& s) {
    std::string out; out.reserve(s.size()+8);
    for (char c: s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[7]; std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

static inline std::string ptr_to_hex(const void* p) {
    std::ostringstream oss; oss << "0x" << std::hex << (uintptr_t)p; return oss.str();
}

// histograma simple potencias de 2: [lo,hi)
struct Bin { uint64_t lo, hi, bytes, count; };
static std::vector<Bin> build_bins(const std::vector<MetricsAggregator::BlockInfo>& blocks) {
    const uint64_t min_b = 16;
    const uint64_t max_b = 16ull * 1024 * 1024;
    std::vector<Bin> bins;
    for (uint64_t lo=min_b; lo<max_b; lo<<=1) bins.push_back(Bin{lo, lo<<1, 0, 0});
    bins.push_back(Bin{max_b, UINT64_MAX, 0, 0}); // catch-all

    for (const auto& b: blocks) {
        uint64_t sz = b.size;
        size_t i=0;
        for (; i+1<bins.size(); ++i) if (sz>=bins[i].lo && sz<bins[i].hi) break;
        bins[i].count++; bins[i].bytes += sz;
    }
    return bins;
}

// ---------- API C expuesta por memprof_api.h ----------
extern "C" void memprof_record_alloc(void* ptr, std::size_t sz, const char* file, int line) {
    g_agg.onAlloc(
        ptr_to_hex(ptr),
        (uint64_t)sz,
        now_ns(),
        file ? std::string(file) : std::string("unknown"),
        line,
        std::string("alloc"),
        false /*is_realloc*/
    );
}

extern "C" void memprof_record_free(void* ptr) {
    g_agg.onFree(ptr_to_hex(ptr), 0 /* hinted_size opcional */);
}

static void tx_loop();

extern "C" int memprof_init(const char* host, int port) {
    std::lock_guard<std::mutex> lk(g_cfg_mtx);
    if (host && *host) g_host = host;
    g_port = port;
    if (g_running.load()) return 0;
    g_running = true;
    g_start_ms = now_ms();
    g_tx_thread = std::thread(tx_loop);
    return 0;
}

extern "C" void memprof_shutdown() {
    g_running = false;
    if (g_tx_thread.joinable()) g_tx_thread.join();
}

// ---------- hilo de transmisión ----------
static void tx_loop() {
    TcpClient client;

    const uint64_t k_period_ms = 1000; // 1 Hz
    uint64_t last_ms = now_ms();

    while (g_running.load()) {
        uint64_t t0 = now_ms();
        if (t0 - last_ms < k_period_ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        last_ms = t0;
        const uint64_t uptime_ms = t0 - g_start_ms;

        // --- recopila del agregador
        auto timeline = g_agg.getTimeline();     // usa t_ns y leak_bytes
        auto files    = g_agg.getFileStats();    // por archivo
        auto blocks   = g_agg.getBlocks();       // vivos actuales
        auto bins     = build_bins(blocks);      // histograma tamaños

        // Deriva métricas de cabecera
        uint64_t live_bytes = 0;
        for (const auto& b : blocks) live_bytes += b.size;

        uint64_t heap_current  = live_bytes;                // alias de compatibilidad
        uint64_t active_allocs = (uint64_t)blocks.size();

        // Asegura al menos 1 punto de timeline para que la GUI “mueva”
        if (timeline.empty()) {
            MetricsAggregator::TimelinePoint p;
            p.t_ns = (uptime_ms * 1000000ULL);
            p.leak_bytes = live_bytes;
            timeline.push_back(p);
        }

        // --- JSON (con alias para compatibilidad)
        std::ostringstream ss;
        ss << "{";
        ss << "\"type\":\"snapshot\",";
        ss << "\"ts_ms\":" << uptime_ms << ',';

        // Campos “nuevos”
        ss << "\"heap_current\":" << heap_current << ',';
        ss << "\"active_allocs\":" << active_allocs << ',';

        // Alias para implementaciones previas del ServerWorker/GUI
        ss << "\"heap_bytes\":"   << heap_current << ',';   // alias
        ss << "\"live_allocs\":"  << active_allocs << ',';  // alias

        // timeline: [[t_ms, bytes]]
        ss << "\"timeline\":[";
        for (size_t i=0;i<timeline.size();++i) {
            if (i) ss << ',';
            const uint64_t t_ms = (uint64_t)(timeline[i].t_ns / 1000000ULL);
            ss << '[' << t_ms << ',' << timeline[i].leak_bytes << ']';
        }
        ss << "],";

        // per_file: [{file,total,allocs,frees,net}]  + alias "files"
        ss << "\"per_file\":[";
        {
            bool first=true;
            for (const auto& kv: files) {
                const auto& f = kv.second;
                const uint64_t frees = (f.alloc_count>=f.live_count) ? (f.alloc_count - f.live_count) : 0;
                if (!first) ss << ','; first=false;
                ss << '{'
                   << "\"file\":\"" << json_escape(kv.first) << "\","
                   << "\"total\":"  << f.alloc_bytes << ','
                   << "\"allocs\":" << f.alloc_count << ','
                   << "\"frees\":"  << frees << ','
                   << "\"net\":"    << f.live_bytes
                   << '}';
            }
        }
        ss << "],";

        // bins: [{lo,hi,bytes,count}] + alias "histogram"
        ss << "\"bins\":[";
        for (size_t i=0;i<bins.size();++i) {
            if (i) ss << ',';
            ss << '{'
               << "\"lo\":"    << bins[i].lo    << ','
               << "\"hi\":"    << bins[i].hi    << ','
               << "\"bytes\":" << bins[i].bytes << ','
               << "\"count\":" << bins[i].count
               << '}';
        }
        ss << "],";

        // leaks = bloques vivos (la GUI aplica umbral/orden)
        ss << "\"leaks\":[";
        for (size_t i=0;i<blocks.size();++i) {
            const auto& b = blocks[i];
            if (i) ss << ',';
            ss << '{'
               << "\"ptr\":\""  << json_escape(b.ptr)  << "\","
               << "\"size\":"   << b.size              << ','
               << "\"file\":\"" << json_escape(b.file) << "\","
               << "\"line\":"   << b.line              << ','
               << "\"type\":\"" << json_escape(b.type) << "\","
               << "\"ts_ns\":"  << b.ts_ns
               << '}';
        }
        ss << "]";

        // Aliases adicionales para máxima compatibilidad
        ss << ",\"files\":"     << "null";  // si tu ServerWorker antiguo miraba 'files', existe la clave
        ss << ",\"histogram\":" << "null";

        ss << "}";

        const std::string payload = ss.str();

        // Envío robusto con reconexión perezosa
        if (!client.send(payload)) {
            (void)client.connectTo(g_host.c_str(), g_port);
            (void)client.send(payload);
        }
    }
}
