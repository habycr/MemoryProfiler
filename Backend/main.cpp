#include "memprof.hpp"
#include <cstdio>

static void my_sink(const memprof::Event& ev) noexcept {
    std::printf("[EV] %s ptr=%p size=%zu file=%s:%d type=%s ts=%llu %s tid=%llu\n",
        ev.kind == memprof::EventKind::Alloc ? "alloc":"free",
        ev.ptr, ev.size,
        ev.file ? ev.file : "(?)",
        ev.line,
        ev.type ? ev.type : "(?)",
        (unsigned long long)ev.timestamp_ns,
        ev.is_array ? "[array]" : "[scalar]",
        (unsigned long long)ev.thread_id
    );
}

int main() {
    memprof::set_sink(&my_sink);
    // ...
}
