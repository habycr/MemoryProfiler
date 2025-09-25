cmake_minimum_required(VERSION 3.20)
project(MemoryProfiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===== Librería común: protocolo + red
add_library(mp_common STATIC
        src/Protocol.cpp
        src/NetUtils.cpp
)
target_include_directories(mp_common PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_features(mp_common PUBLIC cxx_std_20)
if (WIN32)
    target_link_libraries(mp_common PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(mp_common PUBLIC Threads::Threads)
endif()

# ===== Librería tracker (SDK cliente)
add_library(mp_tracker STATIC
        src/MemoryTracker.cpp
)
target_include_directories(mp_tracker PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(mp_tracker PUBLIC mp_common)
target_compile_features(mp_tracker PUBLIC cxx_std_20)
if (WIN32)
    target_link_libraries(mp_tracker PUBLIC ws2_32)
else()
    target_link_libraries(mp_tracker PUBLIC Threads::Threads)
endif()

# ===== Servidor (broker)
add_executable(memory_profiler_server
        src/main.cpp
        src/SocketServer.cpp
)
target_include_directories(memory_profiler_server PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(memory_profiler_server PRIVATE mp_common)
target_compile_features(memory_profiler_server PRIVATE cxx_std_20)
if (WIN32)
    target_link_libraries(memory_profiler_server PRIVATE ws2_32)
else()
    target_link_libraries(memory_profiler_server PRIVATE Threads::Threads)
endif()

# ===== Test de socket/cliente
add_executable(test_socket
        tests/test_socket.cpp
)
target_include_directories(test_socket PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_socket PRIVATE mp_common mp_tracker)
target_compile_features(test_socket PRIVATE cxx_std_20)
if (WIN32)
    target_link_libraries(test_socket PRIVATE ws2_32)
else()
    target_link_libraries(test_socket PRIVATE Threads::Threads)
endif()

# Warnings razonables
if (MSVC)
    foreach(t mp_common mp_tracker memory_profiler_server test_socket)
        target_compile_options(${t} PRIVATE /W4 /permissive-)
    endforeach()
else()
    foreach(t mp_common mp_tracker memory_profiler_server test_socket)
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()
