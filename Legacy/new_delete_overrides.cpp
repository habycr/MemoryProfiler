// src/new_delete_overrides.cpp
#include <new>
#include <cstddef>
#include <cstdlib>
#include <atomic>

#if defined(_MSC_VER) || defined(__MINGW32__)
  #include <malloc.h> // _aligned_malloc, _aligned_free
#endif

#ifdef MEMPROF_ENABLE_REGISTRY
  #include "registry.hpp"
  #define MP_REG_ALLOC_SCALAR(P,SZ,FILE,LINE,TYPE) memprof::register_alloc((P),(SZ),(FILE),(LINE),(TYPE),false)
  #define MP_REG_ALLOC_ARRAY(P,SZ,FILE,LINE,TYPE)  memprof::register_alloc((P),(SZ),(FILE),(LINE),(TYPE),true)
  #define MP_REG_FREE(P)            memprof::register_free((P))
#else
  #define MP_REG_ALLOC_SCALAR(P,SZ,FILE,LINE,TYPE) ((void)0)
  #define MP_REG_ALLOC_ARRAY(P,SZ,FILE,LINE,TYPE)  ((void)0)
  #define MP_REG_FREE(P)                           ((void)0)
#endif

namespace {
// Reentrancia: si true, no registramos (evita loops cuando el registry asigna internamente)
thread_local bool mp_in_new = false;
  // --- RAII para marcar/desmarcar reentrancia de new/delete ---
  struct ReentrancyGuard {
    bool prev;
    ReentrancyGuard() noexcept : prev(mp_in_new) { mp_in_new = true; }
    ~ReentrancyGuard() { mp_in_new = prev; }
  };


// Canal de metadatos capturados por las macros/overloads de file/line/type
thread_local const char* mp_file  = nullptr;
thread_local int         mp_line  = 0;
thread_local const char* mp_type  = nullptr;

// Helpers aligned
inline void* mp_aligned_alloc(std::size_t n, std::size_t alignment) {
#if defined(_MSC_VER) || defined(__MINGW32__)
  if (alignment == 0) alignment = alignof(std::max_align_t);
  return _aligned_malloc(n, alignment);
#else
  if (alignment < sizeof(void*)) alignment = sizeof(void*);
  std::size_t p = 1; while (p < alignment) p <<= 1; alignment = p;
  std::size_t size = n;
  if (alignment && (size % alignment)) size += alignment - (size % alignment);
  void* ptr = nullptr;
  if (posix_memalign(&ptr, alignment, size) != 0) return nullptr;
  return ptr;
#endif
}
inline void mp_aligned_free(void* p) noexcept {
#if defined(_MSC_VER) || defined(__MINGW32__)
  _aligned_free(p);
#else
  std::free(p);
#endif
}

// Consume (y limpia) el contexto actual para no “contaminar” la siguiente asignación
struct Ctx {
  const char* file; int line; const char* type;
};
inline Ctx consume_ctx() {
  Ctx c{ mp_file, mp_line, mp_type };
  mp_file = nullptr; mp_line = 0; mp_type = nullptr;
  return c;
}
} // anon

// ======================================================
//              new / delete (ESCALAR)
// ======================================================
void* operator new(std::size_t n) {
  if (n == 0) n = 1;

  if (mp_in_new) { // reentrante: NO registrar
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc();
    return p;
  }

  mp_in_new = true;
  void* p = std::malloc(n);
  if (!p) { mp_in_new = false; throw std::bad_alloc(); }
  auto c = consume_ctx();
  MP_REG_ALLOC_SCALAR(p, n, c.file, c.line, c.type);
  mp_in_new = false;
  return p;
}

void operator delete(void* p) noexcept {
  if (!p) return;

  if (mp_in_new) { std::free(p); return; }

  mp_in_new = true;
  MP_REG_FREE(p);
  std::free(p);
  mp_in_new = false;
}

// nothrow / sized
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  try { return ::operator new(n); } catch (...) { return nullptr; }
}
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }

// ======================================================
//              new[] / delete[] (ARREGLOS)
// ======================================================
void* operator new[](std::size_t n) {
  if (n == 0) n = 1;

  if (mp_in_new) {
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc();
    return p;
  }

  mp_in_new = true;
  void* p = std::malloc(n);
  if (!p) { mp_in_new = false; throw std::bad_alloc(); }
  auto c = consume_ctx();
  MP_REG_ALLOC_ARRAY(p, n, c.file, c.line, c.type);
  mp_in_new = false;
  return p;
}

void operator delete[](void* p) noexcept {
  if (!p) return;

  if (mp_in_new) { std::free(p); return; }

  mp_in_new = true;
  MP_REG_FREE(p);
  std::free(p);
  mp_in_new = false;
}

void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  try { return ::operator new[](n); } catch (...) { return nullptr; }
}
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete[](p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete[](p); }

// ======================================================
//          ALIGNED new / delete (C++17)
// ======================================================
void* operator new(std::size_t n, std::align_val_t al) {
  if (n == 0) n = 1;
  std::size_t alignment = static_cast<std::size_t>(al);

  if (mp_in_new) {
    void* p = mp_aligned_alloc(n, alignment);
    if (!p) throw std::bad_alloc();
    return p;
  }

  mp_in_new = true;
  void* p = mp_aligned_alloc(n, alignment);
  if (!p) { mp_in_new = false; throw std::bad_alloc(); }
  auto c = consume_ctx();
  MP_REG_ALLOC_SCALAR(p, n, c.file, c.line, c.type);
  mp_in_new = false;
  return p;
}
void operator delete(void* p, std::align_val_t al) noexcept {
  if (!p) return;
  (void)al;

  if (mp_in_new) { mp_aligned_free(p); return; }

  mp_in_new = true;
  MP_REG_FREE(p);
  mp_aligned_free(p);
  mp_in_new = false;
}

void* operator new(std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept {
  try { return ::operator new(n, al); } catch (...) { return nullptr; }
}
void operator delete(void* p, std::align_val_t al, const std::nothrow_t&) noexcept {
  ::operator delete(p, al);
}
void operator delete(void* p, std::size_t, std::align_val_t al) noexcept {
  ::operator delete(p, al);
}

// ======================================================
//        ALIGNED new[] / delete[] (C++17)
// ======================================================
void* operator new[](std::size_t n, std::align_val_t al) {
  if (n == 0) n = 1;
  std::size_t alignment = static_cast<std::size_t>(al);

  if (mp_in_new) {
    void* p = mp_aligned_alloc(n, alignment);
    if (!p) throw std::bad_alloc();
    return p;
  }

  mp_in_new = true;
  void* p = mp_aligned_alloc(n, alignment);
  if (!p) { mp_in_new = false; throw std::bad_alloc(); }
  auto c = consume_ctx();
  MP_REG_ALLOC_ARRAY(p, n, c.file, c.line, c.type);
  mp_in_new = false;
  return p;
}
void operator delete[](void* p, std::align_val_t al) noexcept {
  if (!p) return;
  (void)al;

  if (mp_in_new) { mp_aligned_free(p); return; }

  mp_in_new = true;
  MP_REG_FREE(p);
  mp_aligned_free(p);
  mp_in_new = false;
}
void* operator new[](std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept {
  try { return ::operator new[](n, al); } catch (...) { return nullptr; }
}
void operator delete[](void* p, std::align_val_t al, const std::nothrow_t&) noexcept {
  ::operator delete[](p, al);
}
void operator delete[](void* p, std::size_t, std::align_val_t al) noexcept {
  ::operator delete[](p, al);
}

// ======================================================
//  Overloads “placement-like” para capturar file/line/type
//  usadas por las macros del header público
// ======================================================
#ifdef MEMPROF_ENABLE_REGISTRY
#define MP_REG_ALLOC_META(P,SZ,FILE,LINE,TYPE,ISARR) \
memprof::register_alloc((P),(SZ),(FILE),(LINE),(TYPE),(ISARR))
#else
#define MP_REG_ALLOC_META(...) ((void)0)
#endif

// --- Captura file/line (escalares/arrays) ---
void* operator new(std::size_t n, const char* file, int line) {
  if (n == 0) n = 1;
  ReentrancyGuard g;
  void* p = std::malloc(n);
  if (!p) throw std::bad_alloc();
  MP_REG_ALLOC_META(p, n, file, line, nullptr, false);
  return p;
}

void* operator new[](std::size_t n, const char* file, int line) {
  if (n == 0) n = 1;
  ReentrancyGuard g;
  void* p = std::malloc(n);
  if (!p) throw std::bad_alloc();
  MP_REG_ALLOC_META(p, n, file, line, nullptr, true);
  return p;
}

// --- Captura file/line/type (escalares/arrays) ---
void* operator new(std::size_t n, const char* file, int line, const char* type) {
  if (n == 0) n = 1;
  ReentrancyGuard g;
  void* p = std::malloc(n);
  if (!p) throw std::bad_alloc();
  MP_REG_ALLOC_META(p, n, file, line, type, false);
  return p;
}

void* operator new[](std::size_t n, const char* file, int line, const char* type) {
  if (n == 0) n = 1;
  ReentrancyGuard g;
  void* p = std::malloc(n);
  if (!p) throw std::bad_alloc();
  MP_REG_ALLOC_META(p, n, file, line, type, true);
  return p;
}

