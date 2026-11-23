cmake_minimum_required(VERSION 3.16)
project(triskelion_windows LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "sdk-windows builds on Windows only (WinHTTP, CNG). The portable logic, including the Client loop, is tested from core/ on any OS.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg: `vcpkg install openssl:x64-windows-static` then configure with the vcpkg toolchain file.
add_subdirectory(../core core-build)

# The library an app links. Public header: include/triskelion_win/windows_client.hpp
add_library(triskelion_win STATIC
  src/winhttp_client.cpp src/win_appliers.cpp src/install_id.cpp src/windows_client.cpp)
target_include_directories(triskelion_win PUBLIC include PRIVATE src)
target_compile_definitions(triskelion_win PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(triskelion_win PUBLIC triskelion_core PRIVATE winhttp bcrypt shell32 ole32)
if(MSVC)
  target_compile_options(triskelion_win PRIVATE /W4)
endif()

# Optional command-line host for testing a deployment. Built only when this is the top-level project
# (or with -DTRISKELION_BUILD_EXAMPLE=ON), so an app that pulls Triskelion in does not build it.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_tri_example_default ON)
else()
  set(_tri_example_default OFF)
endif()
option(TRISKELION_BUILD_EXAMPLE "Build the triskelion-updater command-line example" ${_tri_example_default})

if(TRISKELION_BUILD_EXAMPLE)
  add_executable(triskelion-updater example/updater.cpp)
  target_link_libraries(triskelion-updater PRIVATE triskelion_win)
endif()

# triskelion.dll: C interface for apps that are not C++ (C#, Python, Electron...).
# Built by default only when this is the top-level project (or -DTRISKELION_BUILD_DLL=ON).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_tri_dll_default ON)
else()
  set(_tri_dll_default OFF)
endif()
option(TRISKELION_BUILD_DLL "Build triskelion.dll (C interface)" ${_tri_dll_default})

if(TRISKELION_BUILD_DLL)
  add_library(triskelion_dll SHARED src/c_api.cpp)
  set_target_properties(triskelion_dll PROPERTIES OUTPUT_NAME triskelion)
  target_compile_definitions(triskelion_dll PRIVATE TRISKELION_WIN_BUILD_DLL)
  target_link_libraries(triskelion_dll PRIVATE triskelion_win)
endif()
