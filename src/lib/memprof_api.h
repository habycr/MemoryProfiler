// --- añadir/asegurar en memprof_api.h ---
#pragma once
#include <cstddef>
#include <new>          // <- necesario para las formas placement delete

extern "C" {
    int  memprof_init(const char* host, int port);
    void memprof_shutdown();

    // hooks de registro
    void memprof_record_alloc(void* ptr, std::size_t sz, const char* file, int line);
    void memprof_record_free (void* ptr);
}

// Placement NEW con file/line (objetos)
inline void* operator new(std::size_t sz, const char* file, int line) {
    void* p = ::operator new(sz);
    memprof_record_alloc(p, sz, file ? file : "unknown", line);
    return p;
}

// Placement NEW con file/line (arreglos)
inline void* operator new[](std::size_t sz, const char* file, int line) {
    void* p = ::operator new[](sz);
    memprof_record_alloc(p, sz, file ? file : "unknown", line);
    return p;
}

// Placement DELETE que empareja si el ctor lanza (objetos)
inline void operator delete(void* p, const char*, int) noexcept {
    ::operator delete(p);
}

// Placement DELETE que empareja si el ctor lanza (arreglos)
inline void operator delete[](void* p, const char*, int) noexcept {
    ::operator delete[](p);
}

// --- macro opcional para envolver 'new' en TU de aplicación ---
// Asegúrate de incluir ESTE header *después* de todas las cabeceras de la STL.
#ifdef MEMPROF_WRAP_NEW
#undef new
#define new new(__FILE__, __LINE__)
#endif
// --- fin bloque ---
