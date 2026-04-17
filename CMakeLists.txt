cmake_minimum_required(VERSION 3.16)
project(crunchfs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)
pkg_check_modules(ZSTD QUIET libzstd)

if(NOT ZSTD_FOUND)
    find_path(ZSTD_INCLUDE_DIRS zstd.h)
    find_library(ZSTD_LIBRARIES zstd)
    if(NOT ZSTD_INCLUDE_DIRS OR NOT ZSTD_LIBRARIES)
        message(FATAL_ERROR "zstd development headers/library not found")
    endif()
endif()

add_executable(passthrough_cfs src/passthrough_cfs.cpp src/conpresslib.cpp)
target_include_directories(passthrough_cfs PRIVATE ${FUSE3_INCLUDE_DIRS} ${ZSTD_INCLUDE_DIRS})
target_link_libraries(passthrough_cfs PRIVATE ${FUSE3_LIBRARIES} ${ZSTD_LIBRARIES})
target_compile_options(passthrough_cfs PRIVATE ${FUSE3_CFLAGS_OTHER} ${ZSTD_CFLAGS_OTHER})
