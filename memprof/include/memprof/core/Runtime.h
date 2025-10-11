#pragma once

#include <cstddef>
#include <new>

// Si algún sitio definió `#define new ...`, lo desactivamos temporalmente
#ifdef new
  #pragma push_macro("new")
  #undef new
  #define MEMPROF_RESTORE_NEW_MACRO 1
#endif

// -----------------------------------------------------------------------------
// Sobre-cargas con (file,line) usadas por el macro `new` del proyecto.
// IMPORTANTE: mantén aquí la implementación que ya tenías (tracking, etc.).
// Si tu versión original hacía más trabajo (registrar la alloc, etc.),
// reemplaza los cuerpos por los tuyos. Estos cuerpos de ejemplo llaman
// al operador global estándar y dejan el tracking a tu infraestructura.
// -----------------------------------------------------------------------------

inline void* operator new(std::size_t sz, const char* file, int line)
{
    // TODO: si tu build original registra aquí, vuelve a ponerlo.
    (void)file; (void)line;
    return ::operator new(sz);
}

inline void* operator new[](std::size_t sz, const char* file, int line)
{
    // TODO: si tu build original registra aquí, vuelve a ponerlo.
    (void)file; (void)line;
    return ::operator new[](sz);
}

// Deletes emparejados que el compilador puede requerir si falla la construcción.
// No se usan en flujo normal, pero deben existir cuando defines placement-new.
inline void operator delete(void* p, const char* /*file*/, int /*line*/) noexcept
{
    ::operator delete(p);
}

inline void operator delete[](void* p, const char* /*file*/, int /*line*/) noexcept
{
    ::operator delete[](p);
}

// Restauramos el macro `new` si estaba definido
#ifdef MEMPROF_RESTORE_NEW_MACRO
#pragma pop_macro("new")
#undef MEMPROF_RESTORE_NEW_MACRO
#endif
