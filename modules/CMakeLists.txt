cmake_minimum_required(VERSION 3.25)

project(HbaModules LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("${PROJECT_SOURCE_DIR}/../cmake/CPM.cmake")
include("${PROJECT_SOURCE_DIR}/../cmake/StudioHelpers.cmake")
include("${PROJECT_SOURCE_DIR}/../cmake/StudioDependencies.cmake")

hba_enable_ccache()
hba_add_nlohmann_json()
hba_add_catch2()

add_subdirectory(dsp)
add_subdirectory(core)

enable_testing()
list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
include(Catch)

add_executable(hba_modules_tests
    dsp/tests/AdaaTests.cpp
    dsp/tests/CurveTests.cpp
    dsp/tests/DspModulesTests.cpp
    dsp/tests/GainTests.cpp
    core/tests/PresetJsonTests.cpp)
hba_build_for_host_only(hba_modules_tests)
target_include_directories(hba_modules_tests PRIVATE "${PROJECT_SOURCE_DIR}/studio-ui")
target_link_libraries(hba_modules_tests PRIVATE hba_core hba_dsp Catch2::Catch2WithMain)
target_compile_options(hba_modules_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
hba_require_nonblocking(hba_modules_tests)
catch_discover_tests(hba_modules_tests DISCOVERY_MODE PRE_TEST)
