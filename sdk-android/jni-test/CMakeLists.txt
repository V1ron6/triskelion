# Builds the JNI bridge against a desktop JDK so the glue can be tested without an Android device.
# Not part of the Android build. Run ./run.sh.
cmake_minimum_required(VERSION 3.16)
project(triskelion_jni_test LANGUAGES C CXX)
find_package(JNI REQUIRED)
add_subdirectory(../../core core-build)
add_library(triskelion_jni SHARED ../src/main/cpp/jni_bridge.cpp)
target_include_directories(triskelion_jni PRIVATE ${JNI_INCLUDE_DIRS})
target_link_libraries(triskelion_jni PRIVATE triskelion_core)
target_compile_options(triskelion_jni PRIVATE -Wall -Wextra)
