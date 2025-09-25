cmake_minimum_required(VERSION 3.16)
project(Proyecto1_Datos2_2025 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(memoryprofiler)

add_executable(demo main.cpp)

# Enlaza objetos + hereda include público
target_link_libraries(demo PRIVATE
        $<TARGET_OBJECTS:memprof_obj>
        memprof_iface
)