#pragma once
#include <new>
#include <cstddef>

// Sobrecargas con file/line: se definen en new_delete_overrides.cpp
void* operator new(std::size_t n, const char* file, int line);
void* operator new[](std::size_t n, const char* file, int line);

// Macro para capturar file/line automáticamente
#ifndef MP_NEW_FILELINE_DISABLED
#define new new(__FILE__, __LINE__)
#endif
