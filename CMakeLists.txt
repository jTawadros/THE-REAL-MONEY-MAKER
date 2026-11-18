cmake_minimum_required(VERSION 3.16)
project(Heuristica LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

file(GLOB_RECURSE HEUR_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER HEUR_SOURCES EXCLUDE REGEX ".*/src/main\\.cpp$")
file(GLOB_RECURSE HEUR_TESTS CONFIGURE_DEPENDS tests/*.cpp)

add_library(heuristica_core STATIC ${HEUR_SOURCES})
target_include_directories(heuristica_core PUBLIC src)
target_compile_options(heuristica_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)

add_executable(heuristica src/main.cpp)
target_link_libraries(heuristica PRIVATE heuristica_core)

add_executable(heuristica_tests ${HEUR_TESTS})
target_link_libraries(heuristica_tests PRIVATE heuristica_core)

enable_testing()
add_test(NAME heuristica_tests COMMAND heuristica_tests)
