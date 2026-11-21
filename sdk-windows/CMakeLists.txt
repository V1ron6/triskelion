cmake_minimum_required(VERSION 3.16)
project(triskelion_windows LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "sdk-windows builds on Windows only (WinHTTP, CNG). The portable logic is tested from core/ on any OS.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg: `vcpkg install openssl:x64-windows-static` then configure with the vcpkg toolchain file.
add_subdirectory(../core core-build)

add_executable(triskelion-updater
  src/main.cpp src/winhttp_client.cpp src/win_appliers.cpp src/install_id.cpp)
target_compile_definitions(triskelion-updater PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(triskelion-updater PRIVATE triskelion_core winhttp bcrypt shell32 ole32)
if(MSVC)
  target_compile_options(triskelion-updater PRIVATE /W4)
endif()
