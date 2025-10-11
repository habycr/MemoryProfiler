cmake_minimum_required(VERSION 3.20)
project(MemoryProfiler LANGUAGES CXX)

# Opciones
option(BUILD_GUI "Build Qt GUI frontend" ON)
option(BUILD_DEMOS "Build demo programs" ON)
option(BUILD_TOOLS "Build developer tools (mock-broker)" OFF)

# C++ y warnings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (MSVC)
    add_compile_options(/W4 /permissive- /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Salidas organizadas (opcional)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ==== Librería de instrumentación ====
add_subdirectory(memprof)

# ==== GUI (Qt) ====
if (BUILD_GUI)
    add_subdirectory(memprof-gui)
endif()


# ==== Herramientas de dev (mock broker) ====
if (BUILD_TOOLS)
    add_subdirectory(tools)
endif()
