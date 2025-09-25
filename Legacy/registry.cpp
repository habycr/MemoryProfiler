#include "registry.hpp"
#include "memprof.hpp"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <cstdio>

namespace {

using clock_t = std::chrono::steady_clock;

inline std::uint64_t now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        clock_t::now().time_since_epoch()).count();
}

inline std::uint64_t thread_id_u64() noexcept {
    auto id = std::this_thread::get_id();
    // hash estable suficiente para métricas
    return std::hash<std::thread::id>{}(id);
}

struct State {
    std::unordered_map<void*, memprof::AllocInfo> live;
    std::mutex mtx;

    std::atomic<std::uint64_t> bytes_current{0};
    std::atomic<std::uint64_t> bytes_peak{0};
    std::atomic<std::uint64_t> allocs_total{0};
    std::atomic<std::uint64_t> allocs_active{0};
    std::atomic<std::uint64_t> idgen{1};

    memprof::Sink sink{nullptr};

    void update_peak(std::uint64_t cur) {
        auto old = bytes_peak.load(std::memory_order_relaxed);
        while (cur > old &&
               !bytes_peak.compare_exchange_weak(old, cur, std::memory_order_relaxed)) { /* spin */ }
    }
};

State& S() {
    static State s;
    return s;
}

} // anon

namespace memprof {

void set_sink(Sink s) noexcept {
    S().sink = s;
}

void register_alloc(void* p,
                    std::size_t size,
                    const char* file,
                    int line,
                    const char* type,
                    bool is_array) noexcept
{
    if (!p) return;
    auto& st = S();
    const auto tns = now_ns();
    const auto tid = thread_id_u64();
    const auto id  = st.idgen.fetch_add(1, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lk(st.mtx);
        AllocInfo ai;
        ai.size        = size;
        ai.file        = file;
        ai.line        = line;
        ai.type        = type;
        ai.timestamp_ns= tns;
        ai.id          = id;
        ai.is_array    = is_array;
        ai.thread_id   = tid;
        st.live[p] = ai;
    }

    const auto cur = st.bytes_current.fetch_add(size, std::memory_order_relaxed) + size;
    st.update_peak(cur);
    st.allocs_total.fetch_add(1, std::memory_order_relaxed);
    st.allocs_active.fetch_add(1, std::memory_order_relaxed);

    if (st.sink) {
        Event ev{ EventKind::Alloc, p, size, type, file, line, tns, is_array, tid };
        st.sink(ev);
    }
}

void register_free(void* p) noexcept {
    if (!p) return;
    auto& st = S();

    std::size_t freed = 0;
    const char* file = nullptr;
    int line = 0;
    const char* type = nullptr;
    bool is_array = false;
    std::uint64_t tns = now_ns();
    std::uint64_t tid = thread_id_u64();

    {
        std::lock_guard<std::mutex> lk(st.mtx);
        auto it = st.live.find(p);
        if (it != st.live.end()) {
            freed    = it->second.size;
            file     = it->second.file;
            line     = it->second.line;
            type     = it->second.type;
            is_array = it->second.is_array;
            st.live.erase(it);
        }
    }

    if (freed) {
        st.bytes_current.fetch_sub(freed, std::memory_order_relaxed);
        st.allocs_active.fetch_sub(1, std::memory_order_relaxed);
    }

    if (st.sink) {
        Event ev{ EventKind::Free, p, freed, type, file, line, tns, is_array, tid };
        st.sink(ev);
    }
}

// Métricas
std::uint64_t current_bytes() noexcept { return S().bytes_current.load(std::memory_order_relaxed); }
std::uint64_t peak_bytes()    noexcept { return S().bytes_peak.load(std::memory_order_relaxed); }
std::uint64_t total_allocs()  noexcept { return S().allocs_total.load(std::memory_order_relaxed); }
std::uint64_t active_allocs() noexcept { return S().allocs_active.load(std::memory_order_relaxed); }

// Dump de fugas
void dump_leaks_to_stdout() noexcept {
    auto& st = S();
    std::lock_guard<std::mutex> lk(st.mtx);
    if (st.live.empty()) {
        std::printf("[memprof] No leaks.\n");
        return;
    }
    std::printf("[memprof] Leaks (%zu):\n", st.live.size());
    for (const auto& [ptr, ai] : st.live) {
        std::printf("  ptr=%p size=%zu file=%s line=%d type=%s ts=%llu %s\n",
            ptr, ai.size,
            ai.file ? ai.file : "(?)",
            ai.line,
            ai.type ? ai.type : "(?)",
            (unsigned long long)ai.timestamp_ns,
            ai.is_array ? "[array]" : "[scalar]"
        );
    }
}

} // namespace memprof
