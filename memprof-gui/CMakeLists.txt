cmake_minimum_required(VERSION 3.20)
project(memprof_gui LANGUAGES CXX)

# AutoMOC/UIC/RCC para Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Detectar Qt5/Qt6
set(QT_REQUIRED_COMPONENTS Widgets Charts Network)
find_package(Qt6 QUIET COMPONENTS ${QT_REQUIRED_COMPONENTS})
if (Qt6_FOUND)
    set(QT_NS Qt6)
else()
    find_package(Qt5 REQUIRED COMPONENTS ${QT_REQUIRED_COMPONENTS})
    set(QT_NS Qt5)
endif()

# Asegurar que el target "memprof" existe: si no está (build directo de esta carpeta),
# añadimos la subcarpeta con ruta ABSOLUTA al repo.
if (NOT TARGET memprof)
    if (EXISTS "${CMAKE_SOURCE_DIR}/memprof/CMakeLists.txt")
        add_subdirectory("${CMAKE_SOURCE_DIR}/memprof" _memprof_build_from_gui)
    else()
        message(FATAL_ERROR "No se encontró la carpeta 'memprof' en ${CMAKE_SOURCE_DIR}/memprof")
    endif()
endif()

# Fuentes de la GUI
add_executable(memory_profiler_gui
        frontend/app/main.cpp
        frontend/app/MainWindow.cpp
        frontend/app/MainWindow.h

        frontend/net/ServerWorker.cpp
        frontend/net/ServerWorker.h

        frontend/model/TableModels.cpp
        frontend/model/TableModels.h
        frontend/model/Reducers.cpp
        frontend/model/Reducers.h

        frontend/tabs/GeneralTab.cpp
        frontend/tabs/GeneralTab.h
        frontend/tabs/MapTab.cpp
        frontend/tabs/MapTab.h
        frontend/tabs/PerFileTab.cpp
        frontend/tabs/PerFileTab.h
        frontend/tabs/LeaksTab.cpp
        frontend/tabs/LeaksTab.h
)

# Includes públicos de la lib
target_include_directories(memory_profiler_gui PRIVATE
        ${CMAKE_SOURCE_DIR}/memprof/include
)
target_include_directories(memory_profiler_gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}         # <- habilita "frontend/..." desde cualquier .cpp del gui
        ${CMAKE_CURRENT_SOURCE_DIR}/frontend
)


# Enlaces
target_link_libraries(memory_profiler_gui PRIVATE
        ${QT_NS}::Widgets
        ${QT_NS}::Charts
        ${QT_NS}::Network
        memprof            # runtime
)

# Salidas organizadas (opcional)
set_target_properties(memory_profiler_gui PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

