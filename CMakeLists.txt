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
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/MainWindow.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tabs/GeneralTab.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tabs/MapTab.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tabs/PerFileTab.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tabs/LeaksTab.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/net/ServerWorker.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model/TableModels.cpp
        # Si usas reducers u otros:
        # ${CMAKE_CURRENT_SOURCE_DIR}/src/model/Reducers.cpp
)

add_executable(memory_profiler_gui ${GUI_SRC})

target_include_directories(memory_profiler_gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
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

# ---------- Librería de instrumentación (SIN Qt) ----------
# En tu CMakeLists.txt, en la librería memprof:
add_library(memprof STATIC
        Legacy/new_delete_overrides.cpp
        Legacy/registry.cpp
        src/MetricsAggregator.cpp
        # src/MetricsCalculator.cpp  # si existe
        src/lib/TcpClient.cpp        # <-- AQUI: usa src/lib si ese es tu path real
        src/lib/memprof_runtime.cpp  # ya usas src/lib aquí, OK


)
target_include_directories(memprof PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/Legacy
)
target_compile_definitions(memprof PRIVATE MEMPROF_ENABLE_REGISTRY)
target_compile_features(memprof PUBLIC cxx_std_20)
if (WIN32)
    target_link_libraries(memprof PUBLIC ws2_32)
    target_compile_definitions(memprof PRIVATE NOMINMAX)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(memprof PRIVATE pthread)
endif()

# ---------- Demos ----------
add_executable(demo_leaks
        examples/demo_leaks.cpp
        examples/alloc_a.cpp
        examples/alloc_b.cpp
)
target_link_libraries(demo_leaks PRIVATE memprof)
target_compile_definitions(demo_leaks PRIVATE MEMPROF_WRAP_NEW)

# Asegura el wrap de __FILE__/__LINE__ si lo usas


# Enlaza con tu runtime/lib de instrumentation si aplica en tu proyecto
# (ajusta el nombre según tu lib real)
# target_link_libraries(demo_leaks PRIVATE memprof_runtime)


add_executable(demo_stress
        examples/demo_stress.cpp
        examples/alloc_a.cpp
        examples/alloc_b.cpp
        examples/alloc_c.cpp
)
target_link_libraries(demo_stress PRIVATE memprof)
# Captura file/line para que "Por archivo" muestre cada .cpp
target_compile_definitions(demo_stress PRIVATE MEMPROF_WRAP_NEW)

# (Opcional) salida bin
# set_target_properties(memory_profiler_gui PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# set_target_properties(demo_leaks          PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# set_target_properties(demo_stress         PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
