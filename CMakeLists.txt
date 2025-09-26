cmake_minimum_required(VERSION 3.20)
project(MemoryProfiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LEGACY_DIR ${CMAKE_SOURCE_DIR}/Legacy)

# ========== Librería común ==========
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

# ========== SDK tracker ==========
add_library(mp_tracker STATIC src/MemoryTracker.cpp)
target_include_directories(mp_tracker PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(mp_tracker PUBLIC mp_common)
target_compile_features(mp_tracker PUBLIC cxx_std_20)

# ========== Servidor (broker) ==========
add_executable(memory_profiler_server
        src/main.cpp
        src/SocketServer.cpp
)
target_include_directories(memory_profiler_server PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(memory_profiler_server PRIVATE mp_common)
if (WIN32)
    target_link_libraries(memory_profiler_server PRIVATE ws2_32)
else()
    target_link_libraries(memory_profiler_server PRIVATE Threads::Threads)
endif()

# ========== Cliente broker (lib) ==========
add_library(mp_brokerclient STATIC src/BrokerClient.cpp)
target_include_directories(mp_brokerclient PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(mp_brokerclient PUBLIC mp_common)
if (WIN32)
    target_link_libraries(mp_brokerclient PUBLIC ws2_32)
endif()

# ========== Métricas (lib) ==========
add_library(mp_metrics STATIC src/MetricsCalculator.cpp)
target_include_directories(mp_metrics PUBLIC ${CMAKE_SOURCE_DIR}/include)
if (NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(mp_metrics PUBLIC Threads::Threads)
endif()

# ========== Ejemplos ==========
add_executable(broker_client_example
        examples/broker_client_example.cpp
)
target_include_directories(broker_client_example PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(broker_client_example PRIVATE mp_brokerclient)

add_executable(metrics_stream_example
        examples/metrics_stream.cpp
)
target_include_directories(metrics_stream_example PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(metrics_stream_example PRIVATE mp_brokerclient mp_metrics)
if (WIN32)
    target_link_libraries(metrics_stream_example PRIVATE ws2_32)
endif()

# ========== Tests ==========
add_executable(test_socket tests/test_socket.cpp)
target_include_directories(test_socket PRIVATE ${CMAKE_SOURCE_DIR}/include ${LEGACY_DIR})
target_link_libraries(test_socket PRIVATE mp_common mp_tracker)

# legacy overrides para test_socket
add_library(memprof_legacy OBJECT
        ${LEGACY_DIR}/new_delete_overrides.cpp
        ${LEGACY_DIR}/registry.cpp
)
target_include_directories(memprof_legacy PRIVATE ${LEGACY_DIR})
target_compile_definitions(memprof_legacy PRIVATE MEMPROF_ENABLE_REGISTRY)
target_link_libraries(test_socket PRIVATE memprof_bridge $<TARGET_OBJECTS:memprof_legacy>)

add_library(memprof_bridge STATIC ${LEGACY_DIR}/LegacyBridge.cpp)
target_include_directories(memprof_bridge PUBLIC ${CMAKE_SOURCE_DIR}/include ${LEGACY_DIR})
target_link_libraries(memprof_bridge PUBLIC mp_common mp_tracker)

add_executable(test_metrics tests/test_metrics.cpp)
target_include_directories(test_metrics PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_metrics PRIVATE mp_metrics)

include(CTest)
add_test(NAME metrics_unit_test COMMAND test_metrics)

# ========== Warnings ==========
if (MSVC)
    foreach(t mp_common mp_tracker memory_profiler_server mp_brokerclient mp_metrics broker_client_example metrics_stream_example test_socket test_metrics)
        target_compile_options(${t} PRIVATE /W4 /permissive- /utf-8)
    endforeach()
else()
    foreach(t mp_common mp_tracker memory_profiler_server mp_brokerclient mp_metrics broker_client_example metrics_stream_example test_socket test_metrics)
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

# ========== Servidor HTTP de métricas + estáticos ==========
add_executable(gui_http_server
        src/gui_http_server.cpp
)
target_include_directories(gui_http_server PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(gui_http_server PRIVATE mp_brokerclient mp_metrics mp_common)
if (WIN32)
    target_link_libraries(gui_http_server PRIVATE ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(gui_http_server PRIVATE Threads::Threads)
endif()
