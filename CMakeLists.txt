cmake_minimum_required(VERSION 3.20)
project(MemoryProfilerSuperbuild LANGUAGES CXX)

option(BUILD_LIB   "Build memprof instrumentation library" ON)
option(BUILD_GUI   "Build Qt GUI app" ON)
option(BUILD_DEMOS "Build demo programs (stress/leaks)" OFF)
option(BUILD_TOOLS "Build tools (mock_broker)" OFF)

add_subdirectory(memprof)

if (BUILD_GUI)
    add_subdirectory(memprof-gui)
endif()

if (BUILD_DEMOS)
    add_subdirectory(demos)
endif()

if (BUILD_TOOLS)
    add_subdirectory(tools)
endif()
