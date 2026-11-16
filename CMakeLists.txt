cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(TractForm VERSION 0.2.0 LANGUAGES C CXX)
if(APPLE)
    # Visage's and JUCE's macOS sources are Objective-C++.
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TF_BUILD_PLUGIN "Build the interface and the plugin (fetches JUCE and Visage)" ON)
option(TF_BUILD_TESTS "Build the tests" ON)
option(TF_INSTALL_PLUGIN "Copy the plugins into ~/Library/Audio/Plug-Ins after building" OFF)

find_program(TF_CCACHE ccache HINTS /opt/homebrew/bin /usr/local/bin)
if(TF_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_C_COMPILER_LAUNCHER "${TF_CCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${TF_CCACHE}")
endif()

# Warnings for our own targets only; dependencies keep their own flags.
add_library(tf_warnings INTERFACE)
target_compile_options(tf_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)

include(cmake/CPM.cmake)
CPMAddPackage(NAME nlohmann_json GITHUB_REPOSITORY nlohmann/json VERSION 3.12.0 SYSTEM YES)

add_subdirectory(core)
add_subdirectory(engine)
add_subdirectory(tools)

if(TF_BUILD_PLUGIN)
    CPMAddPackage(NAME visage GITHUB_REPOSITORY VitalAudio/visage GIT_TAG v1.0.0 SYSTEM YES)
    CPMAddPackage(NAME JUCE GITHUB_REPOSITORY juce-framework/JUCE GIT_TAG 9.0.5 SYSTEM YES)
    add_subdirectory(ui)
    add_subdirectory(plugin)
endif()

if(TF_BUILD_TESTS)
    enable_testing()
    CPMAddPackage(NAME Catch2 GITHUB_REPOSITORY catchorg/Catch2 VERSION 3.16.1 SYSTEM YES)
    list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
    include(Catch)
    add_subdirectory(tests)
endif()
