cmake_minimum_required(VERSION 3.16)
project(c_code_visualizer_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Source Files

add_executable(visualizer
    src/main.cpp
    src/mi_parser.cpp
    src/gdb_controller.cpp
)

target_include_directories(visualizer PRIVATE include)

# Link pthread (needed for some GDB operations)
find_package(Threads REQUIRED)
target_link_libraries(visualizer PRIVATE Threads::Threads util)

# Compiler Warnings

target_compile_options(visualizer PRIVATE
    -Wall -Wextra -Wpedantic
)
