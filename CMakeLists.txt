cmake_minimum_required(VERSION 3.0.0)
project(HotReloader VERSION 0.1.0)

enable_testing()

add_executable(HotReloader main.cpp)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)

find_package(Threads REQUIRED)

target_link_libraries(HotReloader PRIVATE Threads::Threads)
