cmake_minimum_required(VERSION 3.20)
project(MemoryProfilerGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt: habilitar automoc/uic/rcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Intenta Qt6 primero
find_package(Qt6 COMPONENTS Widgets Charts Network QUIET)
if (NOT Qt6_FOUND)
    message(STATUS "Qt6 no encontrado, probando Qt5…")
    find_package(Qt5 COMPONENTS Widgets Charts Network REQUIRED)
    set(QT_PREFIX Qt5)
else()
    set(QT_PREFIX Qt6)
endif()

set(SRC
        src/main.cpp
        src/MainWindow.cpp
        src/tabs/GeneralTab.cpp
        src/tabs/MapTab.cpp
        src/tabs/PerFileTab.cpp
        src/tabs/LeaksTab.cpp
        src/net/MetricsWorker.cpp
        src/model/Reducers.cpp
        src/model/TableModels.cpp
)

add_executable(memory_profiler_gui ${SRC})
target_include_directories(memory_profiler_gui PRIVATE src)
target_link_libraries(memory_profiler_gui PRIVATE
        ${QT_PREFIX}::Widgets
        ${QT_PREFIX}::Charts
        ${QT_PREFIX}::Network
)

# Windows: evitar consola de consola en app GUI
if (WIN32)
    set_target_properties(memory_profiler_gui PROPERTIES WIN32_EXECUTABLE TRUE)
    # Opcional: evitar conflictos con min/max macros
    target_compile_definitions(memory_profiler_gui PRIVATE NOMINMAX)
endif()

# --- Mock opcional (TCP) ---
add_executable(mock_broker tools/mock_broker.cpp)
if (WIN32)
    target_link_libraries(mock_broker PRIVATE ws2_32)
endif()
