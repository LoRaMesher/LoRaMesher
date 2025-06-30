cmake_minimum_required(VERSION 3.14)
project(LoRaMesher VERSION 1.0.0 LANGUAGES CXX)

# Include ESP-IDF project.cmake only when not building for desktop
if(NOT DEFINED BUILD_DESKTOP OR NOT BUILD_DESKTOP)
    if(DEFINED ENV{PLATFORMIO})
        include($ENV{PLATFORMIO_CMAKE_TOOLCHAIN_DIR}/project.cmake)
    elseif(DEFINED ENV{IDF_PATH})
        include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    else()
        message(STATUS "ESP-IDF path not found. Building without ESP-IDF support.")
        # Continue without ESP-IDF for desktop builds
    endif()
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
elseif(MSVC)
    add_compile_options(/W4 /WX)
endif()

# Generate compile_commands.json for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(BUILD_TESTING "Build tests" ON)
option(BUILD_DESKTOP "Build for desktop environment instead of embedded" OFF)

# Download and configure RadioLib
include(FetchContent)
FetchContent_Declare(
    radiolib
    GIT_REPOSITORY https://github.com/jgromes/RadioLib.git
    GIT_TAG 7.1.2
)
FetchContent_MakeAvailable(radiolib)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Platform-specific configurations
if(BUILD_DESKTOP)
    message(STATUS "Configuring for desktop build")
    # For desktop builds, ARDUINO is not defined, so system_config.hpp 
    # will automatically use LORAMESHER_BUILD_NATIVE
elseif(ESP_PLATFORM)
    message(STATUS "Configuring for ESP-IDF build")
    # When building with ESP-IDF, it will use its own component registration
    # Will define ARDUINO=100 in src/CMakeLists.txt
else()
    message(STATUS "Configuring for PlatformIO ESP32 build")
    # Will define ARDUINO=100 in src/CMakeLists.txt
endif()

# Add main library
add_subdirectory(src)

# Add tests if enabled
if(BUILD_TESTING AND (BUILD_DESKTOP OR CMAKE_TESTING_ENABLED))
    enable_testing()
    add_subdirectory(test)
endif()

# Print configuration
message(STATUS "Configuration:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build testing: ${BUILD_TESTING}")
message(STATUS "  Build for desktop: ${BUILD_DESKTOP}")