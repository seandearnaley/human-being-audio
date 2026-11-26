cmake_minimum_required(VERSION 3.25)

# Stage Link and Stage Sound: the two plugins that feed the Stage desktop app. See README.md.
#   cmake --preset dev -DCPM_SOURCE_CACHE="$HOME/.cache/CPM" && cmake --build --preset dev && ctest --preset dev
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(StagePlugin VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(STAGE_INSTALL_PLUGIN "Copy the plugins into ~/Library/Audio/Plug-Ins after building" OFF)

set(STUDIO_CMAKE "${PROJECT_SOURCE_DIR}/../../../cmake")
include("${STUDIO_CMAKE}/CPM.cmake")
include("${STUDIO_CMAKE}/StudioHelpers.cmake")
include("${STUDIO_CMAKE}/StudioDependencies.cmake")
hba_enable_ccache()
hba_add_juce()

# Two plugins from the same sources, told apart at compile time by JucePlugin_IsSynth. Stage Link
# is an instrument: it goes on a track and reports that track's transport, notes and macros. Stage
# Sound is an effect: it goes last on the main track, passes the mix through untouched and sends it
# to the Stage, so a take has the song's real sound.
function(stage_add_plugin target name bundle code synth category)
    juce_add_plugin(${target}
        PRODUCT_NAME "${name}"
        DESCRIPTION "Sends transport, MIDI, macros and sound to the Stage"
        COMPANY_NAME "Human Being Audio"
        COMPANY_WEBSITE "https://humanbeingaudio.com"
        BUNDLE_ID "${bundle}"
        PLUGIN_MANUFACTURER_CODE Hbmu
        PLUGIN_CODE ${code}
        FORMATS VST3 AU
        IS_SYNTH ${synth}
        NEEDS_MIDI_INPUT ${synth}
        NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES ${category}
        COPY_PLUGIN_AFTER_BUILD ${STAGE_INSTALL_PLUGIN})
    target_sources(${target} PRIVATE src/Capture.h src/Link.cpp src/Link.h src/Plugin.cpp src/Plugin.h)
    target_include_directories(${target} PUBLIC src)
    target_include_directories(${target} SYSTEM INTERFACE "${JUCE_SOURCE_DIR}/modules")
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_MODAL_LOOPS_PERMITTED=0)
    target_link_libraries(${target}
        PRIVATE juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
endfunction()

stage_add_plugin(StageLink "Stage Link" com.humanbeingmusic.stagelink Stg1 TRUE Instrument)
stage_add_plugin(StageSound "Stage Sound" com.humanbeingmusic.stagesound Stg2 FALSE Fx)

# Studio warnings and the enforced non-blocking check, on our sources only (not on JUCE's).
set_source_files_properties(src/Link.cpp src/Plugin.cpp test/host.cpp PROPERTIES COMPILE_OPTIONS
    "-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow;-Werror;-Wfunction-effects;-Werror=function-effects")

# The headless host links Stage Link's shared code directly, as the studio's plugin tests do.
add_executable(stage-link-host test/host.cpp)
target_link_libraries(stage-link-host PRIVATE StageLink)
hba_build_for_host_only(stage-link-host)

enable_testing()
add_test(NAME stage-link-host
    COMMAND "${CMAKE_COMMAND}" "-DHOST=$<TARGET_FILE:stage-link-host>" -P "${PROJECT_SOURCE_DIR}/test/run-host.cmake")
set_tests_properties(stage-link-host PROPERTIES TIMEOUT 60)
