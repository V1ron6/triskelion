cmake_minimum_required(VERSION 3.16)
project(triskelion_core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)  # linked into shared libraries (the DLL, the Android JNI library)

# OpenSSL >= 1.1.1 for Ed25519 + SHA-256.
#   Windows/Linux/macOS: find_package(OpenSSL) (Windows: vcpkg install openssl).
#   Android: the NDK ships none, so use the prefab package "openssl" (Gradle dependency
#   com.android.ndk.thirdparty:openssl), which exports the target openssl::crypto.
if(ANDROID)
  find_package(openssl REQUIRED CONFIG)
  set(TRISKELION_CRYPTO_TARGET openssl::crypto)
else()
  find_package(OpenSSL REQUIRED)
  set(TRISKELION_CRYPTO_TARGET OpenSSL::Crypto)
endif()

add_library(triskelion_core STATIC src/version.cpp src/verify.cpp src/decide.cpp src/json.cpp src/updater.cpp src/client.cpp src/capi.cpp)
target_include_directories(triskelion_core PUBLIC include)
find_package(Threads REQUIRED)
target_link_libraries(triskelion_core PUBLIC ${TRISKELION_CRYPTO_TARGET} Threads::Threads)
if(MSVC)
  target_compile_options(triskelion_core PRIVATE /W4)
else()
  target_compile_options(triskelion_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Tests build only when core is the top-level project, or when asked for with -DTRISKELION_BUILD_TESTS=ON,
# so an app that pulls Triskelion in does not also build them.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_tri_tests_default ON)
else()
  set(_tri_tests_default OFF)
endif()
option(TRISKELION_BUILD_TESTS "Build the triskelion core tests" ${_tri_tests_default})

if(TRISKELION_BUILD_TESTS)
  enable_testing()
  foreach(t core_tests updater_tests client_tests capi_tests)
    add_executable(${t} test/${t}.cpp)
    target_link_libraries(${t} PRIVATE triskelion_core)
    target_compile_definitions(${t} PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
    add_test(NAME ${t} COMMAND ${t})
  endforeach()

  # The public C header must stay valid C, not just C++.
  add_library(capi_header_c OBJECT test/capi_header_check.c)
  target_link_libraries(capi_header_c PRIVATE triskelion_core)
endif()
