cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(Cutline VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CMakeDependentOption)
cmake_dependent_option(CL_BUILD_PLUGIN "Build the interface and the plugin (fetches JUCE and Visage)" ON "NOT HBA_RTSAN" OFF)
option(CL_BUILD_TESTS "Build the tests" ON)
option(CL_INSTALL_PLUGIN "Copy the plugins into ~/Library/Audio/Plug-Ins after building" OFF)

find_program(CL_CCACHE ccache HINTS /opt/homebrew/bin /usr/local/bin)
if(CL_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_C_COMPILER_LAUNCHER "${CL_CCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CL_CCACHE}")
endif()

add_library(cl_warnings INTERFACE)
target_compile_options(cl_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)

include("${PROJECT_SOURCE_DIR}/../../cmake/CPM.cmake")
include("${PROJECT_SOURCE_DIR}/../../cmake/StudioHelpers.cmake")
CPMAddPackage(NAME nlohmann_json GITHUB_REPOSITORY nlohmann/json VERSION 3.12.0 SYSTEM YES)

add_subdirectory("${PROJECT_SOURCE_DIR}/../../modules/dsp" "${PROJECT_BINARY_DIR}/studio-dsp")
add_subdirectory("${PROJECT_SOURCE_DIR}/../../modules/core" "${PROJECT_BINARY_DIR}/studio-core")
add_subdirectory(core)
add_subdirectory(engine)
add_subdirectory(tools)

if(CL_BUILD_PLUGIN)
    CPMAddPackage(NAME visage GITHUB_REPOSITORY VitalAudio/visage GIT_TAG v1.0.0 SYSTEM YES)
    CPMAddPackage(NAME JUCE GITHUB_REPOSITORY juce-framework/JUCE GIT_TAG 9.0.5 SYSTEM YES)
    add_subdirectory("${PROJECT_SOURCE_DIR}/../../modules/visage-ui" "${PROJECT_BINARY_DIR}/studio-visage-ui")
    add_subdirectory("${PROJECT_SOURCE_DIR}/../../modules/plugin-host" "${PROJECT_BINARY_DIR}/studio-plugin-host")
    add_subdirectory(ui)
    add_subdirectory(plugin)
endif()

if(CL_BUILD_TESTS)
    enable_testing()
    CPMAddPackage(NAME Catch2 GITHUB_REPOSITORY catchorg/Catch2 VERSION 3.16.1 SYSTEM YES)
    list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
    include(Catch)
    add_subdirectory(tests)
endif()
