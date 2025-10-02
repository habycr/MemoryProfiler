#pragma once
#include <cstddef>
#include <new>      // std::nothrow_t, std::align_val_t (si hiciera falta)

//
// API C que expone el runtime (debe coincidir 1:1 con memprof_runtime.cpp)
//
extern "C" {
    int  memprof_init(const char* host, int port);
    void memprof_shutdown();

    void memprof_record_alloc(void* ptr, std::size_t sz, const char* file, int line);
    void memprof_record_free (void* ptr);
}

//
// Placement new/new[] con (file,line). IMPORTANTE:
//  - Estas OVERLOADS deben declararse ANTES de activar el macro `new`.
//  - No meterlas dentro de extern "C".
//
inline void* operator new(std::size_t sz, const char* file, int line) {
    void* p = ::operator new(sz);
    memprof_record_alloc(p, sz, file ? file : "unknown", line);
    return p;
}
inline void* operator new[](std::size_t sz, const char* file, int line) {
    void* p = ::operator new[](sz);
    memprof_record_alloc(p, sz, file ? file : "unknown", line);
    return p;
}
// Estos deletes solo emparejan si el ctor lanza:
inline void operator delete(void* p, const char*, int) noexcept { ::operator delete(p); }
inline void operator delete[](void* p, const char*, int) noexcept { ::operator delete[](p); }

//
// Activar el wrapper SOLO al final, para no interferir con las declaraciones de arriba.
// Asegúrate de incluir este header DESPUÉS de la STL en los TUs donde uses el wrapper.
//
#ifdef MEMPROF_WRAP_NEW
#ifndef MEMPROF_WRAP_NEW_APPLIED
#define MEMPROF_WRAP_NEW_APPLIED 1
// No hagas #undef new aquí; normalmente no está definido aún.
// A partir de este punto, cada `new` se reescribe como placement con (file,line).
#define new new(__FILE__, __LINE__)
#endif
#endif
