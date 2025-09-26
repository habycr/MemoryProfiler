#include "MetricsAggregator.h"
#include <chrono>
#include <cctype>
#include <sstream>
#include <algorithm>

MetricsAggregator::MetricsAggregator(size_t timeline_capacity)
    : timeline_cap_(timeline_capacity ? timeline_capacity : 4096) {}

uint64_t MetricsAggregator::now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
uint64_t MetricsAggregator::now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// ------- JSON helpers (similares a los de MetricsCalculator, auto-contenidos) -------
namespace {
inline void skipSpaces(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}
bool parseJSONStringAt(const std::string& s, size_t& i, std::string& out) {
    skipSpaces(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    std::string acc;
    while (i < s.size()) {
        char c = s[i++];
        if (c == '\\') {
            if (i >= s.size()) break;
            char e = s[i++];
            if (e == '"' || e == '\\' || e == '/') acc.push_back(e);
            else if (e == 'b') acc.push_back('\b');
            else if (e == 'f') acc.push_back('\f');
            else if (e == 'n') acc.push_back('\n');
            else if (e == 'r') acc.push_back('\r');
            else if (e == 't') acc.push_back('\t');
            else acc.push_back(e);
        } else if (c == '"') {
            out.swap(acc);
            return true;
        } else {
            acc.push_back(c);
        }
    }
    return false;
}
bool seekFieldValue(const std::string& json, const std::string& field, size_t& i) {
    const std::string quoted = "\"" + field + "\"";
    size_t pos = json.find(quoted, i);
    if (pos == std::string::npos) return false;
    pos += quoted.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != ':') return false;
    ++pos;
    i = pos;
    return true;
}
} // anon

bool MetricsAggregator::extractString(const std::string& json, const std::string& field, std::string& out) {
    size_t i = 0;
    if (!seekFieldValue(json, field, i)) return false;
    return parseJSONStringAt(json, i, out);
}
bool MetricsAggregator::extractBool(const std::string& json, const std::string& field, bool& out) {
    size_t i = 0;
    if (!seekFieldValue(json, field, i)) return false;
    skipSpaces(json, i);
    if (json.compare(i, 4, "true") == 0) { out = true;  return true; }
    if (json.compare(i, 5, "false") == 0){ out = false; return true; }
    return false;
}
bool MetricsAggregator::extractUint64(const std::string& json, const std::string& field, uint64_t& out) {
    size_t i = 0;
    if (!seekFieldValue(json, field, i)) return false;
    skipSpaces(json, i);
    if (i < json.size() && json[i] == '"') {
        std::string tmp;
        if (!parseJSONStringAt(json, i, tmp)) return false;
        if (tmp.rfind("0x", 0) == 0 || tmp.rfind("0X", 0) == 0) {
            char* endp = nullptr;
            unsigned long long v = std::strtoull(tmp.c_str(), &endp, 16);
            out = static_cast<uint64_t>(v);
            return true;
        } else {
            char* endp = nullptr;
            unsigned long long v = std::strtoull(tmp.c_str(), &endp, 10);
            out = static_cast<uint64_t>(v);
            return true;
        }
    } else {
        uint64_t v = 0; bool any = false;
        while (i < json.size() && std::isdigit(static_cast<unsigned char>(json[i]))) {
            any = true;
            v = v * 10 + static_cast<uint64_t>(json[i] - '0');
            ++i;
        }
        if (!any) return false;
        out = v;
        return true;
    }
}
bool MetricsAggregator::extractInt(const std::string& json, const std::string& field, int& out) {
    uint64_t u = 0;
    if (!extractUint64(json, field, u)) return false;
    out = static_cast<int>(u);
    return true;
}

// ------- lógica -------
void MetricsAggregator::onAlloc(const std::string& ptr, uint64_t size, uint64_t ts_ns,
                                const std::string& file, int line,
                                const std::string& type, bool is_array) {
    total_allocs_.fetch_add(1, std::memory_order_relaxed);
    active_allocs_.fetch_add(1, std::memory_order_relaxed);
    uint64_t cur = current_bytes_.fetch_add(size, std::memory_order_relaxed) + size;

    // peak
    uint64_t old_peak = peak_bytes_.load(std::memory_order_relaxed);
    while (cur > old_peak &&
           !peak_bytes_.compare_exchange_weak(old_peak, cur, std::memory_order_relaxed)) {}

    {
        std::lock_guard<std::mutex> lk(mtx_);
        // registrar bloque
        BlockInfo bi;
        bi.ptr = ptr; bi.size = size; bi.file = file; bi.line = line; bi.type = type; bi.is_array = is_array; bi.ts_ns = ts_ns;
        live_[ptr] = bi;

        // actualizar por archivo
        auto& fs = per_file_[file];
        fs.alloc_count += 1;
        fs.alloc_bytes += size;
        fs.live_count  += 1;
        fs.live_bytes  += size;

        // timeline (usar now_ns para eje t)
        uint64_t t = now_ns();
        uint64_t leak_b = computeLeakBytes_locked(t);
        pushTimelinePoint_locked(t, cur, leak_b);
    }
}

void MetricsAggregator::onFree(const std::string& ptr, uint64_t hinted_size) {
    uint64_t sub = 0;
    std::string file;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = live_.find(ptr);
        if (it != live_.end()) {
            sub = it->second.size;
            file = it->second.file;
            // actualizar por archivo
            auto fit = per_file_.find(file);
            if (fit != per_file_.end()) {
                if (fit->second.live_count > 0)  fit->second.live_count -= 1;
                if (fit->second.live_bytes >= sub) fit->second.live_bytes -= sub;
                else fit->second.live_bytes = 0;
            }
            live_.erase(it);
        }

        if (sub > 0) {
            // timeline point después de restar
            uint64_t cur_after = current_bytes_.load(std::memory_order_relaxed) - sub; // aún no aplicado
            uint64_t t = now_ns();
            uint64_t leak_b = computeLeakBytes_locked(t);
            // (no aún: actualizamos current fuera del lock para no mezclar)
            // guardamos el punto al final tras ajustar current_bytes_
            // para ser exactos, lo añadimos después del fetch_sub más abajo
            // aquí no empujamos
        }
    }

    if (sub > 0) {
        current_bytes_.fetch_sub(sub, std::memory_order_relaxed);
        active_allocs_.fetch_sub(1, std::memory_order_relaxed);

        // push timeline con valores ya aplicados
        std::lock_guard<std::mutex> lk(mtx_);
        uint64_t t = now_ns();
        uint64_t cur = current_bytes_.load(std::memory_order_relaxed);
        uint64_t leak_b = computeLeakBytes_locked(t);
        pushTimelinePoint_locked(t, cur, leak_b);
    } else {
        (void)hinted_size;
    }
}

uint64_t MetricsAggregator::computeLeakBytes_locked(uint64_t now_ns_val) const {
    const uint64_t threshold_ns = leak_threshold_ms_.load(std::memory_order_relaxed) * 1000000ULL;
    uint64_t leak = 0;
    for (const auto& kv : live_) {
        const auto& bi = kv.second;
        if (now_ns_val > bi.ts_ns && (now_ns_val - bi.ts_ns) > threshold_ns) {
            leak += bi.size;
        }
    }
    return leak;
}

void MetricsAggregator::pushTimelinePoint_locked(uint64_t t_ns, uint64_t cur_b, uint64_t leak_b) {
    timeline_.push_back(TimelinePoint{t_ns, cur_b, leak_b});
    if (timeline_.size() > timeline_cap_) timeline_.pop_front();
}

void MetricsAggregator::computeLeaksKPIs_locked(uint64_t now_ns_val, LeaksKPIs& out) const {
    const uint64_t threshold_ns = leak_threshold_ms_.load(std::memory_order_relaxed) * 1000000ULL;
    // Por archivo
    std::unordered_map<std::string, std::pair<uint64_t/*count*/, uint64_t/*bytes*/>> per_file_leaks;

    uint64_t count_leaks = 0;
    uint64_t total_leak_b = 0;

    // largest
    uint64_t max_b = 0;
    std::string max_ptr, max_file;

    for (const auto& kv : live_) {
        const auto& bi = kv.second;
        if (now_ns_val > bi.ts_ns && (now_ns_val - bi.ts_ns) > threshold_ns) {
            ++count_leaks;
            total_leak_b += bi.size;
            auto& pf = per_file_leaks[bi.file];
            pf.first  += 1;
            pf.second += bi.size;

            if (bi.size > max_b) {
                max_b = bi.size;
                max_ptr = bi.ptr;
                max_file= bi.file;
            }
        }
    }

    // top_file_by_leaks
    std::string top_file;
    uint64_t top_count = 0, top_bytes = 0;
    for (const auto& kv : per_file_leaks) {
        if (kv.second.first > top_count || (kv.second.first == top_count && kv.second.second > top_bytes)) {
            top_file  = kv.first;
            top_count = kv.second.first;
            top_bytes = kv.second.second;
        }
    }

    out.total_leak_bytes = total_leak_b;
    const uint64_t total_allocs = total_allocs_.load(std::memory_order_relaxed);
    out.leak_rate = (total_allocs > 0) ? (double)count_leaks / (double)total_allocs : 0.0;
    out.largest.file = max_file;
    out.largest.ptr  = max_ptr;
    out.largest.size = max_b;
    out.top_file_by_leaks.file  = top_file;
    out.top_file_by_leaks.count = top_count;
    out.top_file_by_leaks.bytes = top_bytes;
}

void MetricsAggregator::processEvent(const std::string& json) {
    std::string kind;
    if (!extractString(json, "kind", kind)) return;

    if (kind == "ALLOC") {
        std::string ptr, file, type;
        uint64_t size = 0, ts_ns = 0;
        int line = 0; bool is_arr = false;
        extractString(json, "ptr", ptr);
        extractUint64(json, "size", size);
        extractUint64(json, "ts_ns", ts_ns);
        extractString(json, "file", file);
        extractInt   (json, "line", line);
        extractString(json, "type", type);
        extractBool  (json, "is_array", is_arr);
        if (!ptr.empty() && size > 0) onAlloc(ptr, size, ts_ns, file, line, type, is_arr);
    } else if (kind == "FREE") {
        std::string ptr; uint64_t hinted = 0;
        extractString(json, "ptr", ptr);
        extractUint64(json, "size", hinted);
        if (!ptr.empty()) onFree(ptr, hinted);
    }
}

void MetricsAggregator::getMetrics(uint64_t& current_bytes,
                                   uint64_t& peak_bytes,
                                   uint64_t& active_allocs,
                                   uint64_t& total_allocs,
                                   uint64_t& leak_bytes) const {
    current_bytes = current_bytes_.load(std::memory_order_relaxed);
    peak_bytes    = peak_bytes_.load(std::memory_order_relaxed);
    active_allocs = active_allocs_.load(std::memory_order_relaxed);
    total_allocs  = total_allocs_.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(mtx_);
    leak_bytes    = computeLeakBytes_locked(now_ns());
}

std::vector<MetricsAggregator::TimelinePoint> MetricsAggregator::getTimeline() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return std::vector<TimelinePoint>(timeline_.begin(), timeline_.end());
}

std::vector<MetricsAggregator::BlockInfo> MetricsAggregator::getBlocks() const {
    std::vector<BlockInfo> out;
    std::lock_guard<std::mutex> lk(mtx_);
    out.reserve(live_.size());
    for (const auto& kv : live_) out.push_back(kv.second);
    return out;
}

std::unordered_map<std::string, MetricsAggregator::FileStats> MetricsAggregator::getFileStats() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return per_file_;
}

MetricsAggregator::LeaksKPIs MetricsAggregator::getLeaksKPIs() const {
    LeaksKPIs k{};
    std::lock_guard<std::mutex> lk(mtx_);
    computeLeaksKPIs_locked(now_ns(), k);
    return k;
}

void MetricsAggregator::setLeakThresholdMs(uint64_t ms) {
    leak_threshold_ms_.store(ms, std::memory_order_relaxed);
}
uint64_t MetricsAggregator::getLeakThresholdMs() const {
    return leak_threshold_ms_.load(std::memory_order_relaxed);
}
