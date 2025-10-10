cmake_minimum_required(VERSION 3.20)
project(MemprofTools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mock-broker mock_broker.cpp)
if (WIN32)
    target_link_libraries(mock-broker PRIVATE ws2_32)
endif()
