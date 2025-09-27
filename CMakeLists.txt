cmake_minimum_required(VERSION 3.20)
project(MemoryProfilerGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------- Qt: automoc/uic/rcc ----------
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ---------- Busca Qt6, si no Qt5 ----------
find_package(Qt6 COMPONENTS Widgets Charts Network QUIET)
if (NOT Qt6_FOUND)
    message(STATUS "Qt6 no encontrado, probando Qt5…")
    find_package(Qt5 COMPONENTS Widgets Charts Network REQUIRED)
    set(QT_PREFIX Qt5)
else()
    set(QT_PREFIX Qt6)
endif()

# ---------- Fuentes GUI ----------
set(GUI_SRC
        src/main.cpp
        src/MainWindow.cpp
        src/tabs/GeneralTab.cpp
        src/tabs/MapTab.cpp
        src/tabs/PerFileTab.cpp
        src/tabs/LeaksTab.cpp
        src/net/ServerWorker.cpp
        src/model/Reducers.cpp
        src/model/TableModels.cpp
)

add_executable(memory_profiler_gui ${GUI_SRC})
target_include_directories(memory_profiler_gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}        # raíz (para "Legacy/..."/"include/...")
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(memory_profiler_gui PRIVATE
        ${QT_PREFIX}::Widgets
        ${QT_PREFIX}::Charts
        ${QT_PREFIX}::Network
)
if (WIN32)
    set_target_properties(memory_profiler_gui PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(memory_profiler_gui PRIVATE NOMINMAX)
endif()

# ---------- Mock opcional (servidor TCP sintético) ----------
add_executable(mock_broker tools/mock_broker.cpp)
if (WIN32)
    target_link_libraries(mock_broker PRIVATE ws2_32)
endif()

# ---------- Librería de instrumentación (SIN Qt) ----------
add_library(memprof STATIC
        Legacy/new_delete_overrides.cpp
        Legacy/registry.cpp
        src/MetricsAggregator.cpp
        src/MetricsCalculator.cpp
        src/lib/TcpClient.cpp
        src/lib/memprof_runtime.cpp
)
target_include_directories(memprof PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}        # raíz (para "Legacy/..."/"include/...")
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/Legacy
)
target_compile_definitions(memprof PRIVATE MEMPROF_ENABLE_REGISTRY)
target_compile_features(memprof PUBLIC cxx_std_20)
if (WIN32)
    target_link_libraries(memprof PRIVATE ws2_32)
    target_compile_definitions(memprof PRIVATE NOMINMAX)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(memprof PRIVATE pthread)
endif()

# ---------- Demo simple ----------
add_executable(demo_leaks examples/demo_leaks.cpp)
target_link_libraries(demo_leaks PRIVATE memprof)
# Si quisieras capturar file/line aquí también:
# target_compile_definitions(demo_leaks PRIVATE MEMPROF_WRAP_NEW)

# ---------- Demo que estresa todas las vistas ----------
add_executable(demo_stress
        examples/demo_stress.cpp
        examples/alloc_a.cpp
        examples/alloc_b.cpp
        examples/alloc_c.cpp
)
target_link_libraries(demo_stress PRIVATE memprof)
# Captura file/line para que "Top archivos" muestre cada .cpp
target_compile_definitions(demo_stress PRIVATE MEMPROF_WRAP_NEW)

# ---------- (Opcional) Salida en /bin dentro del build dir ----------
# set_target_properties(memory_profiler_gui PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# set_target_properties(mock_broker         PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# set_target_properties(demo_leaks          PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# set_target_properties(demo_stress         PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
