cmake_minimum_required(VERSION 3.16)
project(crunchfs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)

add_executable(passthrough_cfs src/passthrough_cfs.cpp)
target_include_directories(passthrough_cfs PRIVATE ${FUSE3_INCLUDE_DIRS})
target_link_libraries(passthrough_cfs PRIVATE ${FUSE3_LIBRARIES})
target_compile_options(passthrough_cfs PRIVATE ${FUSE3_CFLAGS_OTHER})
