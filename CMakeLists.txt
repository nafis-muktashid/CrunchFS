cmake_minimum_required(VERSION 3.16)
project(CrunchFS CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find FUSE3
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)

# Find zstd
find_library(ZSTD_LIB zstd REQUIRED)
find_path(ZSTD_INC zstd.h REQUIRED)

add_executable(passthrough_cfs
    src/main.cpp
    src/fuse_ops.cpp
    src/metadata.cpp
    src/chunk_store.cpp
    src/thread_pool.cpp
    src/conpresslib.cpp
)

target_include_directories(passthrough_cfs PRIVATE
    ${FUSE3_INCLUDE_DIRS}
    ${ZSTD_INC}
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_options(passthrough_cfs PRIVATE
    ${FUSE3_CFLAGS_OTHER}
    -D_FILE_OFFSET_BITS=64
    -Wall -Wextra -O2
)

target_link_libraries(passthrough_cfs PRIVATE
    ${FUSE3_LIBRARIES}
    ${ZSTD_LIB}
    pthread
)
