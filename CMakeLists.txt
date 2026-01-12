cmake_minimum_required(VERSION 3.20)

project(GraphColoring
        VERSION 1.0
        DESCRIPTION "Parallel Graph Coloring: Serial, Threads, MPI"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---- core library (NO mains here) ----
add_library(core
        src/graph.cpp
        src/io.cpp
        src/solver_common.cpp
        src/solver_serial.cpp
        src/solver_threads.cpp
        src/generate.cpp
        src/solver_k2.cpp
        src/solver_greedy.cpp
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---- main unified executable ----
add_executable(main src/main.cpp)
target_link_libraries(main PRIVATE core)

# threads
find_package(Threads REQUIRED)
target_link_libraries(main PRIVATE Threads::Threads)

# MPI (optional)
find_package(MPI)
if(MPI_CXX_FOUND)
    message(STATUS "MPI found: ${MPI_CXX_COMPILER}")
    target_link_libraries(main PRIVATE MPI::MPI_CXX)
    target_compile_definitions(main PRIVATE HAS_MPI=1)
else()
    message(STATUS "MPI not found: MPI mode will be disabled")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(main PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: C++${CMAKE_CXX_STANDARD}")
