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
find_package (glog REQUIRED)
find_package (gflags REQUIRED)

#
# add_compile_definitions(_QPL_SOFTWARE_)

# C++ library
add_library(compression_engine compression_engine.cc)
target_include_directories(compression_engine PUBLIC $ENV{QPL_PATH}/include/)
target_link_directories(compression_engine PUBLIC $ENV{QPL_PATH}/lib)
target_link_libraries(compression_engine qpl dl glog gflags)

# C library
add_library(compression_engine-c compression_engine-c.cc)
target_link_libraries(compression_engine-c compression_engine)


#
add_executable(compression_engine_demo compression_engine_demo.cc)
target_link_libraries(compression_engine_demo compression_engine glog gflags)
