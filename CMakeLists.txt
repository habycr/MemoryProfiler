cmake_minimum_required(VERSION 3.20)
project(MemoryProfiler LANGUAGES CXX)

# ================== Opciones ==================
option(BUILD_GUI "Build Qt GUI frontend" ON)
option(BUILD_DEMOS "Build demo programs" ON)
option(BUILD_LEGACY_OVERRIDES "Build legacy global new/delete overrides" OFF)
option(ENABLE_SANITIZERS "Enable ASan/UBSan in Debug (GCC/Clang)" OFF)

# ================== C++ / Salidas ==================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if (MSVC)
    add_compile_options(/W4 /permissive- /EHsc)
    if (NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
if (ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if (ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options($<$<CONFIG:Debug>:-fsanitize=address,undefined -fno-omit-frame-pointer>)
    add_link_options   ($<$<CONFIG:Debug>:-fsanitize=address,undefined>)
endif()

# ================== Qt (solo GUI) ==================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if (BUILD_GUI)
    find_package(Qt6 COMPONENTS Widgets Charts Network QUIET)
    if (NOT Qt6_FOUND)
        find_package(Qt5 REQUIRED COMPONENTS Widgets Charts Network)
        set(QT_NS Qt5)
    else()
        set(QT_NS Qt6)
    endif()
endif()

include(GNUInstallDirs)

# ================== Protocolo (headers compartidos) ==================
add_library(memprof-proto INTERFACE)
target_include_directories(memprof-proto INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ================== Backend (lib memprof, SIN Qt) ==================
set(_BACKEND_CANDIDATES
        src/backend/core/MetricsAggregator.cpp
        src/backend/core/TcpClient.cpp
        src/backend/core/Runtime.cpp
        src/backend/core/MetricsCalculator.cpp       # opcional: se filtra si no existe
)

if (BUILD_LEGACY_OVERRIDES)
    list(APPEND _BACKEND_CANDIDATES
            src/backend/legacy/new_delete_overrides.cpp
            src/backend/legacy/registry.cpp
    )
endif()

set(MEMPROF_BACKEND_SOURCES "")
foreach(f IN LISTS _BACKEND_CANDIDATES)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
        list(APPEND MEMPROF_BACKEND_SOURCES "${f}")
    else()
        message(STATUS "Skipping missing backend source: ${f}")
    endif()
endforeach()

add_library(memprof STATIC ${MEMPROF_BACKEND_SOURCES})
target_include_directories(memprof PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# Backend Qt-free
target_compile_definitions(memprof PUBLIC MEMPROF_NO_QT=1)
set_target_properties(memprof PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(memprof PUBLIC cxx_std_20)

if (WIN32)
    target_link_libraries(memprof PUBLIC ws2_32)
    target_compile_definitions(memprof PRIVATE NOMINMAX)
elseif(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(memprof PUBLIC Threads::Threads)
endif()

# ================== GUI (Qt) ==================
if (BUILD_GUI)
    set(GUI_SOURCES
            src/frontend/app/main.cpp
            src/frontend/app/MainWindow.cpp
            src/frontend/net/ServerWorker.cpp
            src/frontend/model/TableModels.cpp
            src/frontend/model/Reducers.cpp
            src/frontend/tabs/GeneralTab.cpp
            src/frontend/tabs/MapTab.cpp
            src/frontend/tabs/PerFileTab.cpp
            src/frontend/tabs/LeaksTab.cpp
    )
    # Filtra los que realmente existan
    set(_GUI_EXISTING "")
    foreach(g IN LISTS GUI_SOURCES)
        if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${g}")
            list(APPEND _GUI_EXISTING "${g}")
        else()
            message(STATUS "Skipping missing GUI source: ${g}")
        endif()
    endforeach()

    # Nombre "legacy" del ejecutable + alias moderno
    add_executable(memory_profiler_gui ${_GUI_EXISTING})
    add_executable(memprof-gui ALIAS memory_profiler_gui)

    target_include_directories(memory_profiler_gui PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(memory_profiler_gui PRIVATE
            ${QT_NS}::Widgets
            ${QT_NS}::Charts
            ${QT_NS}::Network
            memprof-proto
    )
    if (WIN32)
        set_target_properties(memory_profiler_gui PROPERTIES WIN32_EXECUTABLE TRUE)
        target_compile_definitions(memory_profiler_gui PRIVATE NOMINMAX)
    endif()
endif()

# ================== Demos ==================
if (BUILD_DEMOS)
    # demo_leaks
    set(DEMO_LEAKS_SOURCES
            examples/demo_leaks.cpp
            examples/alloc_a.cpp
            examples/alloc_b.cpp
    )
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/alloc_c.cpp")
        list(APPEND DEMO_LEAKS_SOURCES examples/alloc_c.cpp)
    endif()

    add_executable(demo_leaks ${DEMO_LEAKS_SOURCES})
    target_link_libraries(demo_leaks PRIVATE memprof $<$<PLATFORM_ID:Windows>:ws2_32>)
    target_compile_definitions(demo_leaks PRIVATE MEMPROF_WRAP_NEW)

    # demo_stress
    set(DEMO_STRESS_SOURCES
            examples/demo_stress.cpp
            examples/alloc_a.cpp
            examples/alloc_b.cpp
    )
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/alloc_c.cpp")
        list(APPEND DEMO_STRESS_SOURCES examples/alloc_c.cpp)
    endif()

    add_executable(demo_stress ${DEMO_STRESS_SOURCES})
    target_link_libraries(demo_stress PRIVATE memprof $<$<PLATFORM_ID:Windows>:ws2_32>)
    target_compile_definitions(demo_stress PRIVATE MEMPROF_WRAP_NEW)
endif()

# ================== Instalación ==================
if (BUILD_GUI)
    install(TARGETS memory_profiler_gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS memprof ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# ================== Mensajes ==================
message(STATUS "Build options:")
message(STATUS "  BUILD_GUI               = ${BUILD_GUI}")
message(STATUS "  BUILD_DEMOS             = ${BUILD_DEMOS}")
message(STATUS "  BUILD_LEGACY_OVERRIDES  = ${BUILD_LEGACY_OVERRIDES}")
message(STATUS "  ENABLE_SANITIZERS       = ${ENABLE_SANITIZERS}")
if (BUILD_GUI)
    message(STATUS "  Qt namespace            = ${QT_NS}")
endif()
