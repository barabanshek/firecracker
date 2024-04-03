# Usage:
#   * export QPL_PATH=/home/nikita/iaa/qpl
#   * define _QPL_SOFTWARE_ for software path

cmake_minimum_required (VERSION 3.16)

# gcc setup
add_definitions(-std=c++17 -O3 -march=native)
add_definitions(-Wall -Wextra -Wabi -Wsign-conversion -Wformat -Wformat-security)
add_definitions(-pedantic)
add_definitions(-fstack-protector -fPIE -fPIC)
add_definitions(-Wno-unused-function)
add_definitions(-D_FORTIFY_SOURCE=2)

# Stuff.
find_package (gflags REQUIRED)

#
# add_compile_definitions(_QPL_SOFTWARE_)

# C++ library
add_library(memory_restorator memory_restorator.cc reap_recorder.cc)
target_include_directories(memory_restorator PUBLIC $ENV{QPL_HOME}/include/)
target_link_directories(memory_restorator PUBLIC $ENV{QPL_HOME}/lib)
target_link_libraries(memory_restorator qpl dl gflags)

# C library
add_library(memory_restorator-c memory_restorator-c.cc)
target_link_libraries(memory_restorator-c memory_restorator)

#
add_executable(memory_restorator_demo memory_restorator_demo.cc)
target_link_libraries(memory_restorator_demo memory_restorator gflags)

# Microbenchmark
add_executable(memory_restoration_micro microbenchmark.cc)
target_link_libraries(memory_restoration_micro PUBLIC benchmark memory_restorator)
