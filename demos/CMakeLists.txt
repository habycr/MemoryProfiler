cmake_minimum_required(VERSION 3.20)
project(MemprofDemos LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Asegura que la lib exista (viene del superbuild)
if (NOT TARGET Memprof::memprof)
    message(FATAL_ERROR "Demos require Memprof::memprof (configure root with -DBUILD_LIB=ON)")
endif()

# Incluye los headers públicos de la lib para #include "memprof/..."
include_directories(${CMAKE_SOURCE_DIR}/memprof/include)

# demo_leaks
add_executable(demo_leaks
        examples/demo_leaks.cpp
        examples/alloc_a.cpp
        examples/alloc_b.cpp
)
target_link_libraries(demo_leaks PRIVATE Memprof::memprof $<$<PLATFORM_ID:Windows>:ws2_32>)
target_compile_definitions(demo_leaks PRIVATE MEMPROF_WRAP_NEW=1)

# demo_stress
add_executable(demo_stress
        examples/demo_stress.cpp
        examples/alloc_a.cpp
        examples/alloc_b.cpp
        examples/alloc_c.cpp
)
target_link_libraries(demo_stress PRIVATE Memprof::memprof $<$<PLATFORM_ID:Windows>:ws2_32>)
target_compile_definitions(demo_stress PRIVATE MEMPROF_WRAP_NEW=1)
