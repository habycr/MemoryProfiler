cmake_minimum_required(VERSION 3.20)
project(MemprofGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Widgets Charts Network QUIET)
if (NOT Qt6_FOUND)
    find_package(Qt5 REQUIRED COMPONENTS Widgets Charts Network)
    set(QT_NS Qt5)
else()
    set(QT_NS Qt6)
endif()

set(GUI_SOURCES
        frontend/app/main.cpp
        frontend/app/MainWindow.cpp
        frontend/net/ServerWorker.cpp
        frontend/model/TableModels.cpp
        frontend/model/Reducers.cpp
        frontend/tabs/GeneralTab.cpp
        frontend/tabs/MapTab.cpp
        frontend/tabs/PerFileTab.cpp
        frontend/tabs/LeaksTab.cpp
)

add_executable(memory_profiler_gui ${GUI_SOURCES})
add_executable(memprof-gui ALIAS memory_profiler_gui)

# Headers disponibles desde la raíz de memprof-gui y la API pública de la lib
target_include_directories(memory_profiler_gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}                  # permite "frontend/..."
        ${CMAKE_CURRENT_SOURCE_DIR}/../memprof/include  # permite "memprof/..."
)

target_link_libraries(memory_profiler_gui PRIVATE
        ${QT_NS}::Widgets
        ${QT_NS}::Charts
        ${QT_NS}::Network
)

if (WIN32)
    set_target_properties(memory_profiler_gui PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(memory_profiler_gui PRIVATE NOMINMAX)
endif()
