cmake_minimum_required(VERSION 3.16)
project(triskelion_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# OpenSSL >= 1.1.1 for Ed25519 + SHA-256. Windows: vcpkg install openssl. Android: prefab "openssl" AAR.
find_package(OpenSSL REQUIRED)

add_library(triskelion_core STATIC src/version.cpp src/verify.cpp src/decide.cpp)
target_include_directories(triskelion_core PUBLIC include)
target_link_libraries(triskelion_core PUBLIC OpenSSL::Crypto)
if(MSVC)
  target_compile_options(triskelion_core PRIVATE /W4)
else()
  target_compile_options(triskelion_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(core_tests test/core_tests.cpp)
target_link_libraries(core_tests PRIVATE triskelion_core)
target_compile_definitions(core_tests PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
add_test(NAME core_tests COMMAND core_tests)
