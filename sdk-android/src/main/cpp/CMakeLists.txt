cmake_minimum_required(VERSION 3.18)
project(triskelion_jni LANGUAGES C CXX)

# The core (and its tests, which are skipped when it is not the top-level project).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../core core-build)

add_library(triskelion_jni SHARED jni_bridge.cpp)
target_link_libraries(triskelion_jni PRIVATE triskelion_core log)
target_compile_options(triskelion_jni PRIVATE -Wall -Wextra)
# 16 KB page size support (required for new Play submissions)
target_link_options(triskelion_jni PRIVATE -Wl,-z,max-page-size=16384)
