cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(PoleField VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PF_BUILD_PLUGIN "Build the interface and the plugin (fetches JUCE and Visage)" ON)
option(PF_BUILD_TESTS "Build the tests" ON)
option(PF_INSTALL_PLUGIN "Copy the plugins into ~/Library/Audio/Plug-Ins after building" OFF)

find_program(PF_CCACHE ccache HINTS /opt/homebrew/bin /usr/local/bin)
if(PF_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_C_COMPILER_LAUNCHER "${PF_CCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${PF_CCACHE}")
endif()

add_library(pf_warnings INTERFACE)
target_compile_options(pf_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)

include(cmake/CPM.cmake)
CPMAddPackage(NAME nlohmann_json GITHUB_REPOSITORY nlohmann/json VERSION 3.12.0 SYSTEM YES)

add_subdirectory(core)
add_subdirectory(engine)

if(PF_BUILD_PLUGIN)
    CPMAddPackage(NAME visage GITHUB_REPOSITORY VitalAudio/visage GIT_TAG v1.0.0 SYSTEM YES)
    CPMAddPackage(NAME JUCE GITHUB_REPOSITORY juce-framework/JUCE GIT_TAG 9.0.5 SYSTEM YES)
    add_subdirectory(ui)
    add_subdirectory(plugin)
endif()

if(PF_BUILD_TESTS)
    enable_testing()
    CPMAddPackage(NAME Catch2 GITHUB_REPOSITORY catchorg/Catch2 VERSION 3.16.1 SYSTEM YES)
    list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
    include(Catch)
    add_subdirectory(tests)
endif()
