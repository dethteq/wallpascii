cmake_minimum_required(VERSION 3.20)
project(wallpascii VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
  # Static runtime: the app is a single portable exe.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
endif()

option(WALLPASCII_BUILD_CLI "Build the command-line tool" ON)
option(WALLPASCII_BUILD_TESTS "Build the tests" ON)
option(WALLPASCII_BUILD_MAC_APP "Build the macOS app (macOS only)" ON)
option(WALLPASCII_BUILD_WIN_APP "Build the Windows app (Windows only)" ON)

add_subdirectory(core)

if(WALLPASCII_BUILD_CLI)
  add_subdirectory(cli)
endif()

if(APPLE AND WALLPASCII_BUILD_MAC_APP)
  enable_language(OBJCXX)
  add_subdirectory(macos)
endif()

if(WIN32 AND WALLPASCII_BUILD_WIN_APP)
  enable_language(RC)
  add_subdirectory(windows)
endif()

if(WALLPASCII_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
