cmake_minimum_required(VERSION 3.25)
project(StudioShowcase LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(STUDIO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Engine-only build for all 6 studio products (zero JUCE / Visage / Catch2 overhead)
set(LIVECUT_BUILD_PLUGIN OFF CACHE BOOL "" FORCE)
set(LIVECUT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TF_BUILD_PLUGIN OFF CACHE BOOL "" FORCE)
set(TF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PF_BUILD_PLUGIN OFF CACHE BOOL "" FORCE)
set(PF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CL_BUILD_PLUGIN OFF CACHE BOOL "" FORCE)
set(CL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TG_BUILD_PLUGIN OFF CACHE BOOL "" FORCE)
set(TG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SB_BUILD_PLUGIN OFF CACHE BOOL "" FORCE)
set(SB_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${STUDIO_ROOT}/products/livecut-redux" livecut EXCLUDE_FROM_ALL)
add_subdirectory("${STUDIO_ROOT}/products/tract-form" tract EXCLUDE_FROM_ALL)
add_subdirectory("${STUDIO_ROOT}/products/pole-field" pole EXCLUDE_FROM_ALL)
add_subdirectory("${STUDIO_ROOT}/products/cutline" cutline EXCLUDE_FROM_ALL)
add_subdirectory("${STUDIO_ROOT}/products/torus-glide" torus EXCLUDE_FROM_ALL)
add_subdirectory("${STUDIO_ROOT}/products/strike-body" strikebody EXCLUDE_FROM_ALL)

add_executable(studio-showcase
    showcase_common.h
    showcase_sources.cpp
    clips_livecut.cpp
    clips_tract_form.cpp
    clips_pole_field.cpp
    clips_cutline.cpp
    clips_torus_glide.cpp
    clips_strike_body.cpp
    render_showcase.cpp)
target_link_libraries(studio-showcase PRIVATE
    LivecutEngine
    tf_engine
    pf_engine
    cl_engine
    tg_engine
    sb_engine
    hba_core
    hba_dsp
    nlohmann_json::nlohmann_json
)
target_compile_options(studio-showcase PRIVATE -O2 -Wall -Wextra)
