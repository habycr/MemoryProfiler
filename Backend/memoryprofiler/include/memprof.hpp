#pragma once
#include <cstdint>

namespace memprof {

    // ---- Datos de eventos que la GUI podría consumir ----
    enum class EventKind : uint8_t { Alloc = 0, Free = 1 };

    struct Event {
        EventKind   kind;
        void*       ptr;
        std::size_t size;          // 0 para Free si no se conoce
        const char* type;          // puede ser nullptr
        const char* file;          // puede ser nullptr
        int         line;          // 0 si no se pasa
        std::uint64_t timestamp_ns;// monotónico
        bool        is_array;      // new[] vs new
        std::uint64_t thread_id;   // hash de std::thread::id
    };

    // Sink/callback para GUI (opcional). Si no se establece, no hace nada.
    using Sink = void(*)(const Event&) noexcept;
    void set_sink(Sink s) noexcept;

    // Métricas básicas
    std::uint64_t current_bytes() noexcept;
    std::uint64_t peak_bytes() noexcept;
    std::uint64_t total_allocs() noexcept;
    std::uint64_t active_allocs() noexcept;

    // (Opcional) Dump de fugas vivas a stdout
    void dump_leaks_to_stdout() noexcept;

    // ----- Macros para capturar file/line y opcionalmente tipo -----
    // 1) Captura file/line de forma global sin cambios al código del usuario:
#ifndef MEMPROF_NO_OVERRIDE_NEW_MACRO
    // Nota: esto intercepta todos los usos de 'new' en TU que incluyan este header.
    void* operator new(std::size_t, const char* file, int line);
    void* operator new[](std::size_t, const char* file, int line);
#define new new(__FILE__, __LINE__)
#endif

    // 2) Si quieres además registrar el tipo en puntos concretos:
    //    Ej: auto p = MP_NEW_OF(int);  o  auto q = MP_NEW_OF(Foo, 1, 2);
#define MP_NEW_OF(T, ...) new(__FILE__, __LINE__, #T) T(__VA_ARGS__)

    // Overloads internos usados por MP_NEW_OF (no es obligatorio usarlos en todo el proyecto)
    void* operator new(std::size_t, const char* file, int line, const char* type);
    void* operator new[](std::size_t, const char* file, int line, const char* type);

} // namespace memprof
