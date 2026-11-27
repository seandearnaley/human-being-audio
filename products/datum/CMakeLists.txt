cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(Datum VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CMakeDependentOption)
cmake_dependent_option(DT_BUILD_PLUGIN "Build the interface and the plugin (fetches JUCE and Visage)" ON "NOT HBA_RTSAN" OFF)
option(DT_BUILD_TESTS "Build the tests" ON)
option(DT_INSTALL_PLUGIN "Copy the plugins into ~/Library/Audio/Plug-Ins after building" OFF)

add_library(dt_warnings INTERFACE)
target_compile_options(dt_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)

include("${PROJECT_SOURCE_DIR}/../../cmake/CPM.cmake")
include("${PROJECT_SOURCE_DIR}/../../cmake/StudioHelpers.cmake")
include("${PROJECT_SOURCE_DIR}/../../cmake/StudioDependencies.cmake")
hba_enable_ccache()
hba_add_nlohmann_json()

add_subdirectory("${PROJECT_SOURCE_DIR}/../../modules/dsp" "${PROJECT_BINARY_DIR}/studio-dsp")
add_subdirectory("${PROJECT_SOURCE_DIR}/../../modules/core" "${PROJECT_BINARY_DIR}/studio-core")
add_subdirectory(core)
add_subdirectory(engine)
add_subdirectory(tools)

if(DT_BUILD_PLUGIN)
    hba_add_visage()
    hba_add_juce()
    add_subdirectory("${PROJECT_SOURCE_DIR}/../../modules/visage-ui" "${PROJECT_BINARY_DIR}/studio-visage-ui")
    add_subdirectory("${PROJECT_SOURCE_DIR}/../../modules/plugin-host" "${PROJECT_BINARY_DIR}/studio-plugin-host")
    add_subdirectory(ui)
    add_subdirectory(plugin)
endif()

if(DT_BUILD_TESTS)
    enable_testing()
    hba_add_catch2()
    list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
    include(Catch)
    add_subdirectory(tests)
endif()
