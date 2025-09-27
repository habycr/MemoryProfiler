#include "MetricsCalculator.h"
#include <mutex>  // asegúrate de tener este include arriba del archivo
#include <sstream>
#include <iomanip>
#include <cctype>
#include <algorithm>

// -------------------------------
// Helpers locales para JSON simple
// -------------------------------

namespace {

// avanza i para saltar espacios
inline void skipSpaces(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

// extrae literal string: "...."
bool parseJSONStringAt(const std::string& s, size_t& i, std::string& out) {
    skipSpaces(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    ++i; // salta comillas inicial
    std::string acc;
    while (i < s.size()) {
        char c = s[i++];
        if (c == '\\') {
            if (i >= s.size()) break;
            char e = s[i++];
            // soporto escapes básicos
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

// busca `"field":` y deja i al inicio del valor
bool seekFieldValue(const std::string& json, const std::string& field, size_t& i) {
    const std::string quoted = "\"" + field + "\"";
    size_t pos = json.find(quoted, i);
    if (pos == std::string::npos) return false;
    pos += quoted.size();
    // saltar espacios y colon
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != ':') return false;
    ++pos;
    i = pos;
    return true;
}

bool extractString(const std::string& json, const std::string& field, std::string& out) {
    size_t i = 0;
    if (!seekFieldValue(json, field, i)) return false;
    return parseJSONStringAt(json, i, out);
}

bool extractBool(const std::string& json, const std::string& field, bool& out) {
    size_t i = 0;
    if (!seekFieldValue(json, field, i)) return false;
    skipSpaces(json, i);
    if (json.compare(i, 4, "true") == 0) { out = true;  return true; }
    if (json.compare(i, 5, "false") == 0){ out = false; return true; }
    return false;
}

bool extractUint64(const std::string& json, const std::string& field, uint64_t& out) {
    size_t i = 0;
    if (!seekFieldValue(json, field, i)) return false;
    skipSpaces(json, i);
    // número (decimal) o "0x..." como string (algunos emisores envían ptr como string)
    if (i < json.size() && json[i] == '"') {
        // intentar leer string y luego parsear (por si viene "1234")
        std::string tmp;
        if (!parseJSONStringAt(json, i, tmp)) return false;
        if (tmp.rfind("0x", 0) == 0 || tmp.rfind("0X", 0) == 0) {
            // valor hexadecimal — convertir a uint64
            char* endp = nullptr;
            unsigned long long v = std::strtoull(tmp.c_str(), &endp, 16);
            out = static_cast<uint64_t>(v);
            return true;
        } else {
            // decimal en string
            char* endp = nullptr;
            unsigned long long v = std::strtoull(tmp.c_str(), &endp, 10);
            out = static_cast<uint64_t>(v);
            return true;
        }
    } else {
        // número crudo
        uint64_t v = 0;
        bool any = false;
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

// Permite leer un entero "line" que viene como número
bool extractInt(const std::string& json, const std::string& field, int& out) {
    uint64_t u = 0;
    if (!extractUint64(json, field, u)) return false;
    out = static_cast<int>(u);
    return true;
}

} // namespace


// -------------------------------
// Métricas -> JSON
// -------------------------------
std::string MetricsSnapshot::toJSON() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{"
        << "\"current_mb\":"   << current_mb   << ","
        << "\"active_allocs\":"<< active_allocs<< ","
        << "\"leak_mb\":"      << leak_mb      << ","
        << "\"peak_mb\":"      << peak_mb      << ","
        << "\"total_allocs\":" << total_allocs << ","
        << "\"timestamp_ms\":" << timestamp_ms
        << "}";
    return oss.str();
}

// -------------------------------
// MetricsCalculator
// -------------------------------
MetricsCalculator::MetricsCalculator() = default;

void MetricsCalculator::processAlloc(const std::string& ptr, uint64_t size, uint64_t ts_ns,
                                     const std::string& file, int line,
                                     const std::string& type, bool is_array)
{
    // Ajuste contadores atómicos
    current_bytes_.fetch_add(size, std::memory_order_relaxed);
    active_allocs_.fetch_add(1, std::memory_order_relaxed);
    total_allocs_.fetch_add(1, std::memory_order_relaxed);

    // Peak
    uint64_t cur = current_bytes_.load(std::memory_order_relaxed);
    uint64_t prev_peak = peak_bytes_.load(std::memory_order_relaxed);
    while (cur > prev_peak && !peak_bytes_.compare_exchange_weak(prev_peak, cur, std::memory_order_relaxed)) {
        // prev_peak actualizado por compare_exchange_weak
    }

    // Registrar bloque vivo
    {
        std::lock_guard<std::mutex> lk(mtx_);
        live_allocs_[ptr] = AllocInfo{ size, ts_ns, file, line, type, is_array };
    }
}

void MetricsCalculator::processFree(const std::string& ptr, uint64_t hinted_size) {
    uint64_t size_to_sub = 0;
    bool found = false;

    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = live_allocs_.find(ptr);
        if (it != live_allocs_.end()) {
            size_to_sub = it->second.size;
            live_allocs_.erase(it);
            found = true;
        }
    }

    if (found) {
        // Si el emisor envía el size en FREE y difiere, preferimos el registrado en ALLOC
        current_bytes_.fetch_sub(size_to_sub, std::memory_order_relaxed);
        active_allocs_.fetch_sub(1, std::memory_order_relaxed);
        // peak no se reduce
    } else {
        // FREE huérfano: ignorar sin afectar contadores
        (void)hinted_size;
    }
}

void MetricsCalculator::processEvent(const std::string& jsonEvent) {
    // Campos mínimos
    std::string kind;
    if (!extractString(jsonEvent, "kind", kind)) return;

    if (kind == "ALLOC") {
        std::string ptr; (void)ptr;
        uint64_t size = 0, ts_ns = 0;
        std::string file, type;
        int line = 0; bool is_arr = false;

        // ptr suele venir como string:
        extractString(jsonEvent, "ptr", ptr);
        extractUint64(jsonEvent, "size", size);
        extractUint64(jsonEvent, "ts_ns", ts_ns);
        extractString(jsonEvent, "file", file);
        extractInt(jsonEvent, "line", line);
        extractString(jsonEvent, "type", type);
        extractBool(jsonEvent, "is_array", is_arr);

        if (!ptr.empty() && size > 0) {
            processAlloc(ptr, size, ts_ns, file, line, type, is_arr);
        }
    }
    else if (kind == "FREE") {
        std::string ptr; (void)ptr;
        uint64_t size = 0;
        extractString(jsonEvent, "ptr", ptr);
        extractUint64(jsonEvent, "size", size); // puede venir, pero no es obligatorio
        if (!ptr.empty()) {
            processFree(ptr, size);
        }
    }
    // otros "kind" se ignoran de momento
}

uint64_t MetricsCalculator::nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

uint64_t MetricsCalculator::calculateLeakBytes() const {
    const uint64_t now_ms = nowMs();
    const uint64_t threshold_ns = leak_threshold_ms_.load(std::memory_order_relaxed) * 1000000ULL;

    uint64_t leak = 0;
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& kv : live_allocs_) {
        const auto& info = kv.second;
        uint64_t age_ns = (now_ms * 1000000ULL);
        if (age_ns > info.timestamp_ns) {
            age_ns -= info.timestamp_ns;
            if (age_ns > threshold_ns) leak += info.size;
        }
    }
    return leak;
}

MetricsSnapshot MetricsCalculator::getSnapshot() const {
    MetricsSnapshot s;
    const double MB = 1024.0 * 1024.0;

    const uint64_t cur_b  = current_bytes_.load(std::memory_order_relaxed);
    const uint64_t peak_b = peak_bytes_.load(std::memory_order_relaxed);

    s.current_mb    = static_cast<double>(cur_b)  / MB;
    s.peak_mb       = static_cast<double>(peak_b) / MB;
    s.active_allocs = active_allocs_.load(std::memory_order_relaxed);
    s.total_allocs  = total_allocs_.load(std::memory_order_relaxed);
    s.timestamp_ms  = nowMs();

    const uint64_t leak_b = calculateLeakBytes();
    s.leak_mb = static_cast<double>(leak_b) / MB;

    return s;
}

void MetricsCalculator::setLeakThresholdMs(uint64_t ms) {
    leak_threshold_ms_.store(ms, std::memory_order_relaxed);
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
    // No tocamos leak_threshold_ms_
}
