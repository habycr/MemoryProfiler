cmake_minimum_required(VERSION 3.16)
project(MemoryProfiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Librería común (Protocol + NetUtils)
add_library(mp_common
        src/Protocol.cpp
        src/NetUtils.cpp   # <— NUEVO
)
target_include_directories(mp_common PUBLIC include)

# Servidor
add_executable(memory_profiler_server
        src/main.cpp
        src/SocketServer.cpp
)
target_include_directories(memory_profiler_server PUBLIC include)
target_link_libraries(memory_profiler_server PRIVATE mp_common)

# Cliente de instrumentación
add_library(mp_tracker
        src/MemoryTracker.cpp
)
target_include_directories(mp_tracker PUBLIC include)
target_link_libraries(mp_tracker PRIVATE mp_common)

# Enlazado específico por OS
if(WIN32)
    target_link_libraries(memory_profiler_server PRIVATE ws2_32)
    target_link_libraries(mp_tracker PRIVATE ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(memory_profiler_server PRIVATE Threads::Threads)
    target_link_libraries(mp_tracker PRIVATE Threads::Threads)
endif()

# Prueba simple
add_executable(test_socket tests/test_socket.cpp)
target_include_directories(test_socket PUBLIC include)
target_link_libraries(test_socket PRIVATE mp_common mp_tracker)

if(WIN32)
    target_link_libraries(test_socket PRIVATE ws2_32)
else()
    target_link_libraries(test_socket PRIVATE Threads::Threads)
endif()
