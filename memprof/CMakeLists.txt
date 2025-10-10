cmake_minimum_required(VERSION 3.20)
project(Memprof LANGUAGES CXX)

option(BUILD_LEGACY_OVERRIDES "Build legacy new/delete overrides into the lib" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4 /permissive- /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)

# Fuentes (rutas relativas a memprof/)
set(MEMPROF_SRC
        backend/core/MetricsAggregator.cpp
        backend/core/MetricsCalculator.cpp
        backend/core/Runtime.cpp
        backend/core/TcpClient.cpp
)

if (BUILD_LEGACY_OVERRIDES)
    list(APPEND MEMPROF_SRC
            Legacy/new_delete_overrides.cpp
            Legacy/registry.cpp
    )
endif()

add_library(memprof STATIC ${MEMPROF_SRC})
add_library(Memprof::memprof ALIAS memprof)

# Backend sin Qt — usa el header dual (shims)
target_compile_definitions(memprof PUBLIC MEMPROF_NO_QT=1)
set_target_properties(memprof PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(memprof PUBLIC cxx_std_20)

target_include_directories(memprof
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/backend
)

if (WIN32)
    target_link_libraries(memprof PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(memprof PUBLIC Threads::Threads)
endif()

# (Opcional) instalación/export
install(TARGETS memprof
        EXPORT MemprofTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT MemprofTargets
        FILE MemprofTargets.cmake
        NAMESPACE Memprof::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Memprof)
