#pragma once
#include <cstddef>
#include <cstdint>

namespace memprof {

    struct AllocInfo {
        std::size_t  size{0};
        const char*  type{nullptr};
        const char*  file{nullptr};
        int          line{0};
        std::uint64_t timestamp_ns{0};
        std::uint64_t id{0};
        bool         is_array{false};
        std::uint64_t thread_id{0};
    };

    void register_alloc(void* p,
                        std::size_t size,
                        const char* file,
                        int line,
                        const char* type,
                        bool is_array) noexcept;

    void register_free(void* p) noexcept;

    // Métricas
    std::uint64_t current_bytes() noexcept;
    std::uint64_t peak_bytes() noexcept;
    std::uint64_t total_allocs() noexcept;
    std::uint64_t active_allocs() noexcept;

    // Control/salida
    void set_sink(void(*s)(const struct Event&) noexcept) noexcept; // definido en memprof.hpp
    void dump_leaks_to_stdout() noexcept;

} // namespace memprof
