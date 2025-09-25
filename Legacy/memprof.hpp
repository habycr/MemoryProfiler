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

} // namespace memprof

// --- Captura de file/line/type: OPT-IN seguro ---
// Si QUIERES redefinir 'new' en TU .cpp, haz:
//   #define MEMPROF_ENABLE_NEW_MACRO
//   #include "memprof.hpp"
// Asegúrate de definirla DESPUÉS de incluir headers de la STL.

#ifdef MEMPROF_ENABLE_NEW_MACRO
// Declaraciones de los placement-like para file/line/type en ÁMBITO GLOBAL
void* operator new (std::size_t, const char* file, int line);
void* operator new[](std::size_t, const char* file, int line);
void* operator new (std::size_t, const char* file, int line, const char* type);
void* operator new[](std::size_t, const char* file, int line, const char* type);

// Macro opcional (no la uses en headers de librerías/3rd-party)
#define new new(__FILE__, __LINE__)

// Alternativas seguras (sin redefinir 'new'):
#define MP_NEW_OF(T, ...)        (::operator new(sizeof(T), __FILE__, __LINE__, #T), new T(__VA_ARGS__))
#define MP_NEW_ARRAY_OF(T, N)    (static_cast<T*>(::operator new[](sizeof(T)*(N), __FILE__, __LINE__, #T)))
#endif
