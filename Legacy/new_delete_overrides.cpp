// Legacy/new_delete_overrides.cpp
#include <new>
#include <cstddef>
#include <cstdlib>
#include <atomic>

#if defined(_MSC_VER) || defined(__MINGW32__)
  #include <malloc.h> // _aligned_malloc, _aligned_free
#endif

// --- OPCIONAL: notificación directa al runtime/GUI ---
// #include "src/lib/memprof_api.h"

// --- Registro opcional (tu registry.hpp legado) ---
#ifdef MEMPROF_ENABLE_REGISTRY
  #include "registry.hpp"
  #define MP_REG_ALLOC_SCALAR(P,SZ,FILE,LINE,TYPE) memprof::register_alloc((P),(SZ),(FILE),(LINE),(TYPE),false)
  #define MP_REG_ALLOC_ARRAY(P,SZ,FILE,LINE,TYPE)  memprof::register_alloc((P),(SZ),(FILE),(LINE),(TYPE),true)
  #define MP_REG_FREE(P)                           memprof::register_free((P))
#else
  #define MP_REG_ALLOC_SCALAR(P,SZ,FILE,LINE,TYPE) ((void)0)
  #define MP_REG_ALLOC_ARRAY(P,SZ,FILE,LINE,TYPE)  ((void)0)
  #define MP_REG_FREE(P)                           ((void)0)
#endif

namespace {
thread_local bool mp_in_new = false;

struct ReentrancyGuard {
  bool prev;
  ReentrancyGuard() noexcept : prev(mp_in_new) { mp_in_new = true; }
  ~ReentrancyGuard() { mp_in_new = prev; }
};

inline void* mp_aligned_alloc(std::size_t n, std::size_t alignment) {
#if defined(_MSC_VER) || defined(__MINGW32__)
  if (alignment == 0) alignment = alignof(std::max_align_t);
  return _aligned_malloc(n, alignment);
#else
  if (alignment < sizeof(void*)) alignment = sizeof(void*);
  void* ptr = nullptr;
  if (posix_memalign(&ptr, alignment, n) != 0) return nullptr;
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
} // anon

// ======================================================
//              new / delete (ESCALAR)
// ======================================================
void* operator new(std::size_t n) {
  if (n == 0) n = 1;

  if (mp_in_new) {
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc();
    return p;
  }

  ReentrancyGuard guard;
  void* p = std::malloc(n);
  if (!p) throw std::bad_alloc();

  MP_REG_ALLOC_SCALAR(p, n, nullptr, 0, nullptr);
  // memprof_record_alloc(p, n, "global_new", 0);
  return p;
}

void operator delete(void* p) noexcept {
  if (!p) return;

  if (mp_in_new) { std::free(p); return; }

  ReentrancyGuard guard;
  // memprof_record_free(p);
  MP_REG_FREE(p);
  std::free(p);
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

  ReentrancyGuard guard;
  void* p = std::malloc(n);
  if (!p) throw std::bad_alloc();

  MP_REG_ALLOC_ARRAY(p, n, nullptr, 0, nullptr);
  // memprof_record_alloc(p, n, "global_new[]", 0);
  return p;
}

void operator delete[](void* p) noexcept {
  if (!p) return;

  if (mp_in_new) { std::free(p); return; }

  ReentrancyGuard guard;
  // memprof_record_free(p);
  MP_REG_FREE(p);
  std::free(p);
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
  const std::size_t alignment = static_cast<std::size_t>(al);

  if (mp_in_new) {
    void* p = mp_aligned_alloc(n, alignment);
    if (!p) throw std::bad_alloc();
    return p;
  }

  ReentrancyGuard guard;
  void* p = mp_aligned_alloc(n, alignment);
  if (!p) throw std::bad_alloc();

  MP_REG_ALLOC_SCALAR(p, n, nullptr, 0, nullptr);
  // memprof_record_alloc(p, n, "global_new_aligned", 0);
  return p;
}

void operator delete(void* p, std::align_val_t al) noexcept {
  if (!p) return; (void)al;

  if (mp_in_new) { mp_aligned_free(p); return; }

  ReentrancyGuard guard;
  // memprof_record_free(p);
  MP_REG_FREE(p);
  mp_aligned_free(p);
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
  const std::size_t alignment = static_cast<std::size_t>(al);

  if (mp_in_new) {
    void* p = mp_aligned_alloc(n, alignment);
    if (!p) throw std::bad_alloc();
    return p;
  }

  ReentrancyGuard guard;
  void* p = mp_aligned_alloc(n, alignment);
  if (!p) throw std::bad_alloc();

  MP_REG_ALLOC_ARRAY(p, n, nullptr, 0, nullptr);
  // memprof_record_alloc(p, n, "global_new[]_aligned", 0);
  return p;
}

void operator delete[](void* p, std::align_val_t al) noexcept {
  if (!p) return; (void)al;

  if (mp_in_new) { mp_aligned_free(p); return; }

  ReentrancyGuard guard;
  // memprof_record_free(p);
  MP_REG_FREE(p);
  mp_aligned_free(p);
}

// ======================================================
//      Overloads adicionales (completan el set)
// ======================================================

// delete[] sized + aligned  (FALTANTE: ahora incluido)
void operator delete[](void* p, std::size_t /*sz*/, std::align_val_t al) noexcept {
  ::operator delete(p, al);
}

// new[] aligned + nothrow  (FALTANTE: ahora incluido)
void* operator new[](std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept {
  try { return ::operator new[](n, al); } catch (...) { return nullptr; }
}

// ======================================================
// NOTA: se eliminaron las sobrecargas “placement-like”
// (file/line[/type]) para no duplicar lo de memprof_api.h.
// ======================================================
