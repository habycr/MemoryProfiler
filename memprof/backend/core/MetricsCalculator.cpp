#include "memprof/core/MetricsCalculator.h"
#include <algorithm>
#include <chrono>
#include <sstream>

// ===== Constructor =====
MetricsCalculator::MetricsCalculator() = default;

// ===== Eventos =====
void MetricsCalculator::processAlloc(const std::string& ptr,
                                     std::uint64_t size,
                                     std::uint64_t ts_ns,
                                     const std::string& file,
                                     int line,
                                     const std::string& type,
                                     bool is_array)
{
    current_bytes_.fetch_add(size, std::memory_order_relaxed);
    active_allocs_.fetch_add(1, std::memory_order_relaxed);
    total_allocs_.fetch_add(1, std::memory_order_relaxed);

    // Peak lock-free
    std::uint64_t cur = current_bytes_.load(std::memory_order_relaxed);
    std::uint64_t prev_peak = peak_bytes_.load(std::memory_order_relaxed);
    while (cur > prev_peak &&
           !peak_bytes_.compare_exchange_weak(prev_peak, cur, std::memory_order_relaxed))
    {
        // retry
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        live_allocs_[ptr] = AllocInfo{ size, ts_ns, file, line, type, is_array };
    }

    if (last_alloc_ts_ns_ > 0 && ts_ns > last_alloc_ts_ns_) {
        const double dt = (ts_ns - last_alloc_ts_ns_) / 1e9; // seg
        if (dt > 0.0) {
            const double inst = 1.0 / dt;
            std::lock_guard<std::mutex> lk(rate_mtx_);
            alloc_rate_ = 0.9 * alloc_rate_ + 0.1 * inst;
        }
    }
    last_alloc_ts_ns_ = ts_ns;
}

void MetricsCalculator::processFree(const std::string& ptr, std::uint64_t /*hinted_size*/) {
    std::uint64_t size_to_sub = 0;

    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = live_allocs_.find(ptr);
        if (it != live_allocs_.end()) {
            size_to_sub = it->second.size;
            live_allocs_.erase(it);
        }
    }

    if (size_to_sub) {
        current_bytes_.fetch_sub(size_to_sub, std::memory_order_relaxed);
        active_allocs_.fetch_sub(1, std::memory_order_relaxed);
    }

    // Marca free-rate con tiempo actual aprox.
    const std::uint64_t ts_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()
    );
    if (last_free_ts_ns_ > 0 && ts_ns > last_free_ts_ns_) {
        const double dt = (ts_ns - last_free_ts_ns_) / 1e9;
        if (dt > 0.0) {
            const double inst = 1.0 / dt;
            std::lock_guard<std::mutex> lk(rate_mtx_);
            free_rate_ = 0.9 * free_rate_ + 0.1 * inst;
        }
    }
    last_free_ts_ns_ = ts_ns;
}

void MetricsCalculator::processEvent(const std::string& /*jsonEvent*/) {
    // No-op por ahora (puedes parsear JSON aquí si lo necesitas)
}

// ===== Config =====
void MetricsCalculator::setLeakThresholdMs(std::uint64_t ms) {
    leak_threshold_ms_.store(ms, std::memory_order_relaxed);
}

// ===== Snapshot =====
MetricsSnapshot MetricsCalculator::getSnapshot() const {
    MetricsSnapshot s;

    const std::uint64_t cur_b  = current_bytes_.load(std::memory_order_relaxed);
    const std::uint64_t peak_b = peak_bytes_.load(std::memory_order_relaxed);

    s.heapCurrent = cur_b;
    s.heapPeak    = peak_b;
    s.activeAllocs= active_allocs_.load(std::memory_order_relaxed);
    s.totalAllocs = total_allocs_.load(std::memory_order_relaxed);
    s.uptimeMs    = nowMs();

    {
        std::lock_guard<std::mutex> lk(rate_mtx_);
        s.allocRate = alloc_rate_;
        s.freeRate  = free_rate_;
    }

    // Bytes que consideramos fugas según umbral de antigüedad
    s.leakBytes = calculateLeakBytes();

    // Rellenar lista de vivos como candidatos a fuga (y marcar isLeak)
    const std::uint64_t threshold_ns =
        leak_threshold_ms_.load(std::memory_order_relaxed) * 1000000ULL;
    const std::uint64_t now_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()
    );

    {
        std::lock_guard<std::mutex> lk(mtx_);
        s.leaks.reserve(live_allocs_.size());
        for (const auto& kv : live_allocs_) {
            // Parse del puntero si viene como "0x..."
            std::uint64_t ptr_value = 0;
            if (!kv.first.empty() && kv.first.rfind("0x", 0) == 0) {
                std::stringstream ss;
                ss << std::hex << kv.first;
                ss >> ptr_value;
            }

            LeakItem li;
            li.ptr   = static_cast<qulonglong>(ptr_value);
            li.size  = static_cast<qlonglong>(kv.second.size);
            li.file  = kv.second.file;  // QString shim (std::string en backend)
            li.line  = kv.second.line;
            li.type  = kv.second.type;
            li.ts_ns = static_cast<qulonglong>(kv.second.ts_ns);
            li.isLeak = (now_ns - kv.second.ts_ns >= threshold_ns);

            s.leaks.push_back(li);
        }
    }

    // KPIs extra (opcional): mayor fuga, archivo top, etc. (se pueden calcular aquí)

    return s;
}

void MetricsCalculator::reset() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        live_allocs_.clear();
    }
    current_bytes_.store(0, std::memory_order_relaxed);
    peak_bytes_.store(0, std::memory_order_relaxed);
    active_allocs_.store(0, std::memory_order_relaxed);
    total_allocs_.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(rate_mtx_);
        alloc_rate_ = 0.0;
        free_rate_  = 0.0;
    }
    last_alloc_ts_ns_ = 0;
    last_free_ts_ns_  = 0;
}

// ===== Helpers =====
std::uint64_t MetricsCalculator::nowMs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count()
    );
}

std::uint64_t MetricsCalculator::calculateLeakBytes() const {
    const std::uint64_t threshold_ns =
        leak_threshold_ms_.load(std::memory_order_relaxed) * 1000000ULL;
    const std::uint64_t now_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()
    );

    std::uint64_t sum = 0;
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& kv : live_allocs_) {
        const auto& a = kv.second;
        if (now_ns - a.ts_ns >= threshold_ns) {
            sum += a.size;
        }
    }
    return sum;
}

// ===== Serialización (simple) =====
QString toJSON(const MetricsSnapshot& s) {
#ifdef MEMPROF_NO_QT
    // En backend, QString es std::string (shim)
    std::ostringstream os;
    os << "{"
       << "\"heapCurrent\":" << s.heapCurrent << ","
       << "\"heapPeak\":"    << s.heapPeak    << ","
       << "\"activeAllocs\":"<< s.activeAllocs<< ","
       << "\"totalAllocs\":" << s.totalAllocs << ","
       << "\"leakBytes\":"   << s.leakBytes   << ","
       << "\"allocRate\":"   << s.allocRate   << ","
       << "\"freeRate\":"    << s.freeRate    << ","
       << "\"uptimeMs\":"    << s.uptimeMs
       << "}";
    return os.str(); // QString == std::string aquí
#else
    // En GUI, si necesitas JSON real usa QJsonDocument/QJsonObject.
    return {};
#endif
}

