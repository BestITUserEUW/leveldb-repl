cmake_minimum_required(VERSION 3.24)

project(leveldb-repl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    oryx-crt-cpp
    GIT_REPOSITORY https://github.com/BestITUserEUW/oryx-crt-cpp.git
    GIT_TAG v0.3.0
    OVERRIDE_FIND_PACKAGE
)

FetchContent_Declare(
    leveldb
    GIT_REPOSITORY https://github.com/google/leveldb.git
    GIT_TAG ac691084fdc5546421a55b25e7653d450e5a25fb
    OVERRIDE_FIND_PACKAGE
)

option(LEVELDB_BUILD_BENCHMARKS "Build LevelDB benchmarks" OFF)
option(LEVELDB_BUILD_TESTS "Build LevelDB tests" OFF)
FetchContent_MakeAvailable(oryx-crt-cpp leveldb)

add_executable(${PROJECT_NAME} src/main.cpp)

target_link_libraries(${PROJECT_NAME} 
    leveldb 
    oryx-crt-cpp
)

target_compile_options(${PROJECT_NAME} PUBLIC
    -Wall -Wextra -Wuninitialized -Wno-unused-function -Wunused-variable $<$<CONFIG:Release>:-g0> $<$<CONFIG:Release>:-O3>
)

target_link_options(${PROJECT_NAME} PUBLIC
    -static-libgcc -static-libstdc++ $<$<CONFIG:Release>:-s>
)