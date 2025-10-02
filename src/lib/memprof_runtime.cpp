// src/lib/memprof_runtime.cpp
#include <atomic>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <string>
#include <cstdint>
#include <cstddef>

#include "MetricsAggregator.h"
#include "TcpClient.h"

// ---------------- Estado global ----------------
static std::atomic<bool> g_running{false};
static std::string       g_host   = "127.0.0.1";
static int               g_port   = 7070;

using steady_clock_t = std::chrono::steady_clock;
static steady_clock_t::time_point g_start_tp;

static MetricsAggregator g_agg;

static inline uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               steady_clock_t::now().time_since_epoch()).count();
}
static inline uint64_t uptime_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               steady_clock_t::now() - g_start_tp).count();
}
static std::string ptr_to_hex(const void* p) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase
        << std::setw(sizeof(void*)*2) << std::setfill('0')
        << reinterpret_cast<std::uintptr_t>(p);
    return oss.str();
}

// ========== API pública que invocan wrappers/overrides ==========
extern "C" {

void memprof_record_alloc(void* ptr, std::size_t sz, const char* file, int line) {
    if (!ptr) return;
    g_agg.onAlloc(
        ptr_to_hex(ptr),
        static_cast<uint64_t>(sz),
        now_ns(),
        file ? std::string(file) : std::string("unknown"),
        line,
        "global_new",
        false /*is_array*/);
}

void memprof_record_free(void* ptr) {
    if (!ptr) return;
    g_agg.onFree(ptr_to_hex(ptr), /*hinted_size*/0);
}

int memprof_init(const char* host, int port) {
    if (host && *host) g_host = host;
    if (port > 0)      g_port = port;
    g_start_tp = steady_clock_t::now();
    g_running.store(true, std::memory_order_relaxed);

    std::thread([]{
        TcpClient client;

        // --- Estado para tasas ---
        uint64_t prev_total_allocs = 0;
        uint64_t prev_active       = 0;
        auto     prev_tp           = steady_clock_t::now();

        while (g_running.load(std::memory_order_relaxed)) {
            if (!client.isConnected()) {
                client.close();
                client.connectTo(g_host.c_str(), g_port);
                if (!client.isConnected()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    continue;
                }
            }

            // ----- snapshot del agregador -----
            const auto timeline = g_agg.getTimeline();
            const auto blocks   = g_agg.getBlocks();
            const auto perfile  = g_agg.getFileStats();

            uint64_t heap_current = 0;
            if (!timeline.empty()) heap_current = timeline.back().cur_bytes;
            else for (const auto& b : blocks) heap_current += b.size;

            uint64_t heap_peak = 0;
            for (const auto& p : timeline) heap_peak = std::max(heap_peak, p.cur_bytes);

            const uint64_t active_allocs = static_cast<uint64_t>(blocks.size());

            MetricsAggregator::LeaksKPIs kpis = g_agg.getLeaksKPIs();

            uint64_t total_allocs = 0;
            for (const auto& kv : perfile) total_allocs += kv.second.alloc_count;

            // --- Tasas alloc/free (aprox) ---
            const auto now_tp = steady_clock_t::now();
            const double dt_s = std::max(0.001,
                std::chrono::duration_cast<std::chrono::duration<double>>(now_tp - prev_tp).count());

            const int64_t d_allocs = (int64_t)total_allocs - (int64_t)prev_total_allocs;
            const int64_t d_active = (int64_t)active_allocs - (int64_t)prev_active;
            // frees ≈ allocs - delta(live)
            const int64_t d_frees  = d_allocs - d_active;

            const double alloc_rate = d_allocs > 0 ? (double)d_allocs / dt_s : 0.0;
            const double free_rate  = d_frees  > 0 ? (double)d_frees  / dt_s : 0.0;

            prev_total_allocs = total_allocs;
            prev_active       = active_allocs;
            prev_tp           = now_tp;

            // bins (por tamaño)
            struct Bin { uint64_t lo, hi, bytes, allocations; };
            std::vector<uint64_t> edges;
            for (uint64_t v = 1; v <= (1ull<<30); v <<= 1) edges.push_back(v);
            edges.push_back(UINT64_C(1) << 62);
            std::vector<Bin> bins(edges.size());
            for (size_t i = 0; i < bins.size(); ++i) {
                bins[i].lo = (i == 0 ? 0ULL : edges[i-1]);
                bins[i].hi = edges[i];
                bins[i].bytes = 0;
                bins[i].allocations = 0;
            }
            auto pick_bin = [&](uint64_t sz)->size_t {
                for (size_t i = 0; i < bins.size(); ++i)
                    if (sz >= bins[i].lo && sz < bins[i].hi) return i;
                return bins.size() - 1;
            };
            for (const auto& b : blocks) {
                auto bi = pick_bin(b.size);
                bins[bi].bytes += b.size;
                bins[bi].allocations += 1;
            }

            // ----- JSON -----
            std::ostringstream ss;
            ss << '{';

            // general + KPIs de fugas y tasas
            ss << "\"general\":{"
               << "\"uptime_ms\":"      << uptime_ms()      << ','
               << "\"heap_current\":"   << heap_current     << ','
               << "\"heap_peak\":"      << heap_peak        << ','
               << "\"active_allocs\":"  << active_allocs    << ','
               << "\"alloc_rate\":"     << alloc_rate       << ','
               << "\"free_rate\":"      << free_rate        << ','
               << "\"total_allocs\":"   << total_allocs     << ','
               << "\"leak_bytes\":"     << kpis.total_leak_bytes << ','
               << "\"leak_rate\":"      << kpis.leak_rate         << ','
               << "\"largest_size\":"   << kpis.largest.size      << ','
               << "\"largest_file\":\"" << kpis.largest.file      << "\","
               << "\"top_file\":\""     << kpis.top_file_by_leaks.file   << "\","
               << "\"top_file_count\":" << kpis.top_file_by_leaks.count  << ','
               << "\"top_file_bytes\":" << kpis.top_file_by_leaks.bytes
               << "},";

            // per_file
            ss << "\"per_file\":[";
            bool first = true;
            for (const auto& kv : perfile) {
                const auto& file = kv.first;
                const auto& fs   = kv.second;
                const uint64_t frees = (fs.alloc_count >= fs.live_count)
                                       ? (fs.alloc_count - fs.live_count) : 0ULL;
                if (!first) ss << ',';
                first = false;
                ss << '{'
                   << "\"file\":\""     << file << "\","
                   << "\"totalBytes\":" << fs.alloc_bytes << ','
                   << "\"allocs\":"     << fs.alloc_count << ','
                   << "\"frees\":"      << frees          << ','
                   << "\"netBytes\":"   << fs.live_bytes
                   << '}';
            }
            ss << "],";

            // bins
            ss << "\"bins\":[";
            for (size_t i = 0; i < bins.size(); ++i) {
                if (i) ss << ',';
                ss << '{'
                   << "\"lo\":"          << bins[i].lo          << ','
                   << "\"hi\":"          << bins[i].hi          << ','
                   << "\"bytes\":"       << bins[i].bytes       << ','
                   << "\"allocations\":" << bins[i].allocations
                   << '}';
            }
            ss << "],";

            // leaks (bloques vivos)
            ss << "\"leaks\":[";
            for (size_t i = 0; i < blocks.size(); ++i) {
                if (i) ss << ',';
                const auto& b = blocks[i];
                ss << '{'
                   << "\"ptr\":\""  << b.ptr  << "\","
                   << "\"size\":"   << b.size << ','
                   << "\"file\":\"" << b.file << "\","
                   << "\"line\":"   << b.line << ','
                   << "\"type\":\"" << b.type << "\","
                   << "\"ts_ns\":"  << b.ts_ns
                   << '}';
            }
            ss << "],";

            // timeline: [t_ms, heap_bytes]
            ss << "\"timeline\":[";
            for (size_t i = 0; i < timeline.size(); ++i) {
                if (i) ss << ',';
                const auto& p = timeline[i];
                const uint64_t t_ms = p.t_ns / 1'000'000ULL;
                ss << '[' << t_ms << ',' << p.cur_bytes << ']';
            }
            ss << ']';

            ss << '}';

            client.sendLine(ss.str());
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }).detach();
    return 0;
}

void memprof_shutdown() {
    g_running.store(false, std::memory_order_relaxed);
}

} // extern "C"
