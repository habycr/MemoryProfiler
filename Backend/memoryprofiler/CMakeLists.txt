cmake_minimum_required(VERSION 3.16)
project(memprof LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1) Objetos con el código
add_library(memprof_obj OBJECT
        src/new_delete_overrides.cpp
        src/registry.cpp
)

# <-- necesario: paths para que los .cpp vean memprof.hpp y registry.hpp
target_include_directories(memprof_obj
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(memprof_obj PRIVATE MEMPROF_ENABLE_REGISTRY)

if (MSVC)
    target_compile_options(memprof_obj PRIVATE /W4)
else()
    target_compile_options(memprof_obj PRIVATE -Wall -Wextra -Wpedantic)
endif()

# (opcional) interfaz para que el ejecutable herede el include público
add_library(memprof_iface INTERFACE)
target_include_directories(memprof_iface
        INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include
)