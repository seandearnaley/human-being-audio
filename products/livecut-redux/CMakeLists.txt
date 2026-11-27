cmake_minimum_required(VERSION 3.25)

# The engine and UI libraries are linked into loadable plugin modules.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Minimum macOS version matching the studio Visage + CPM baseline.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures to build")

project(Livecut VERSION 2.0.3 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CMakeDependentOption)
cmake_dependent_option(LIVECUT_BUILD_PLUGIN "Build the interface and the plugin (fetches JUCE and Visage)" ON "NOT HBA_RTSAN" OFF)
option(LIVECUT_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(LIVECUT_COPY_PLUGIN_AFTER_BUILD "Install the plugins into the user's plug-in folders after building" OFF)

include("${PROJECT_SOURCE_DIR}/../../cmake/CPM.cmake")
include("${PROJECT_SOURCE_DIR}/../../cmake/StudioHelpers.cmake")
include("${PROJECT_SOURCE_DIR}/../../cmake/StudioDependencies.cmake")
hba_enable_ccache()
hba_add_nlohmann_json()

# The cutting engine is plain C++ with no JUCE dependency, so it can be tested in isolation and under RTSan.
add_subdirectory("${PROJECT_SOURCE_DIR}/../../modules/dsp" "${PROJECT_BINARY_DIR}/studio-dsp")
add_subdirectory("${PROJECT_SOURCE_DIR}/../../modules/core" "${PROJECT_BINARY_DIR}/studio-core")

add_library(LivecutEngine STATIC
    src/engine/Accent.h
    src/engine/WetHold.h
    src/engine/AudioHistory.h
    src/engine/SwingGrid.h
    src/engine/DelayTimeline.h
    src/engine/OnsetDetector.h
    src/engine/TransformSpan.h
    src/engine/Bounce.cpp
    src/engine/CutProc11.cpp
    src/engine/Cutter.cpp
    src/engine/Effects.cpp
    src/engine/ImpactEffects.cpp
    src/engine/FrequencyEffects.cpp
    src/engine/Resonator.cpp
    src/engine/RouteRack.cpp
    src/engine/Fracture.cpp
    src/engine/LivePlayer.cpp
    src/engine/Loops.cpp
    src/engine/PadGate.cpp
    src/engine/PadPrograms.cpp
    src/engine/PhraseSeed.cpp
    src/engine/Random.cpp
    src/engine/Rush.cpp
    src/engine/SQPusher.cpp
    src/engine/WarpCut.cpp)
target_include_directories(LivecutEngine PUBLIC src)
target_compile_features(LivecutEngine PUBLIC cxx_std_20)
target_link_libraries(LivecutEngine PUBLIC hba_dsp)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(LivecutEngine PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    hba_require_nonblocking(LivecutEngine)
elseif(MSVC)
    target_compile_options(LivecutEngine PRIVATE /W4)
else()
    target_compile_options(LivecutEngine PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

if(LIVECUT_BUILD_PLUGIN)
    hba_add_visage()
    hba_add_juce()
    add_subdirectory("${PROJECT_SOURCE_DIR}/../../modules/visage-ui" "${PROJECT_BINARY_DIR}/studio-visage-ui")
    add_subdirectory("${PROJECT_SOURCE_DIR}/../../modules/plugin-host" "${PROJECT_BINARY_DIR}/studio-plugin-host")
    add_subdirectory(ui)

    # The FFT adapter and worker are shared by plugin and processor tests.
    add_library(LivecutSpectral STATIC
        src/spectral/SpectralStage.h
        src/spectral/SpectralStage.cpp
        src/spectral/SpectralKernel.h
        src/spectral/SpectralKernel.cpp
        src/spectral/JuceFft.cpp)
    target_include_directories(LivecutSpectral PUBLIC src)
    target_compile_features(LivecutSpectral PUBLIC cxx_std_20)
    target_link_libraries(LivecutSpectral PRIVATE juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    include("${PROJECT_SOURCE_DIR}/../../cmake/StudioProduct.cmake")
    hba_product(livecut-redux)

    juce_add_plugin(Livecut
        PRODUCT_NAME "${HBA_name}"
        DESCRIPTION "BBCut-style live beat slicer"
        COMPANY_NAME "${HBA_publisher}"
        COMPANY_WEBSITE "${HBA_website}"
        BUNDLE_ID "${HBA_bundle}"
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
        PLUGIN_MANUFACTURER_CODE "${HBA_manufacturerCode}"
        PLUGIN_CODE "${HBA_pluginCode}"
        FORMATS VST3 AU Standalone
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES Fx
        AU_MAIN_TYPE kAudioUnitType_MusicEffect
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Livecut Redux cuts the audio coming into your Mac."
        COPY_PLUGIN_AFTER_BUILD ${LIVECUT_COPY_PLUGIN_AFTER_BUILD})

    target_compile_definitions(Livecut
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0)

    file(GLOB LIVECUT_PRESET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json")
    juce_add_binary_data(LivecutPresets
        HEADER_NAME LivecutPresetsData.h
        NAMESPACE LivecutPresetsData
        SOURCES ${LIVECUT_PRESET_FILES})

    # Compile plugin sources once into Livecut (JUCE's SharedCode static archive)
    # so tests link directly instead of recompiling plugin & JUCE.
    target_sources(Livecut
        PRIVATE
            src/CaptureService.cpp
            src/CaptureService.h
            src/Presets.cpp
            src/Presets.h
            src/StreamResources.h
            src/OutputMixer.h
            src/PhraseCapture.cpp
            src/PhraseCapture.h
            src/DisplayState.h
            src/HostSync.cpp
            src/HostSync.h
            src/PadSurface.cpp
            src/PadSurface.h
            src/Parameters.cpp
            src/Parameters.h
            src/PluginModel.cpp
            src/PluginModel.h
            src/PluginEditor.cpp
            src/PluginEditor.h
            src/PluginProcessor.cpp
            src/PluginProcessor.h)
    target_include_directories(Livecut PUBLIC src)
    target_include_directories(Livecut SYSTEM INTERFACE "${JUCE_SOURCE_DIR}/modules")
    target_link_libraries(Livecut
        PRIVATE
            hba_plugin_host
            lc_ui
            LivecutPresets
        PUBLIC
            hba_core
            hba_studio_ui
            hba_plugin_host_headers
            LivecutEngine
            LivecutSpectral
            juce::juce_audio_utils
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

if(LIVECUT_BUILD_TESTS)
    enable_testing()
    hba_add_catch2()
    list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
    include(Catch)

    if(LIVECUT_BUILD_PLUGIN)
        add_executable(LivecutTests
            tests/engine/AllocationCounter.cpp
            tests/SpectralTests.cpp
            tests/ResonanceProcessorTests.cpp
            tests/ResonanceStateTests.cpp
            tests/CaptureTests.cpp
            tests/HostSyncTests.cpp
            tests/ParametersTests.cpp
            tests/PadProcessorTests.cpp
            tests/PluginProcessorTests.cpp
            tests/FractureStateTests.cpp
            tests/FractureProcessorTests.cpp
            tests/ImpactStateTests.cpp
            tests/ImpactProcessorTests.cpp
            tests/RecutProcessorTests.cpp
            tests/Livecut2Tests.cpp)
        hba_build_for_host_only(LivecutTests)
        target_link_libraries(LivecutTests
            PRIVATE
                Livecut
                lc_ui
                Catch2::Catch2WithMain
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags)

        catch_discover_tests(LivecutTests DISCOVERY_MODE PRE_TEST)
    endif()

    add_executable(LivecutEngineTests
        "${PROJECT_SOURCE_DIR}/../../modules/dsp/tests/GainTests.cpp"
        tests/engine/AttackCandidatesTests.cpp
        tests/engine/TransformPlaybackTests.cpp
        tests/engine/RecutSelectionTests.cpp
        tests/legacy/LegacyCutter.cpp
        tests/legacy/LegacyPlayer.cpp
        tests/engine/LegacyEquivalenceTests.cpp
        tests/engine/PerformTests.cpp
        tests/engine/StudioTests.cpp
        tests/engine/TransformSpanTests.cpp
        tests/engine/AllocationCounter.cpp
        tests/engine/AudioHistoryTests.cpp
        tests/engine/SwingGridTests.cpp
        tests/engine/DelayTimelineTests.cpp
        tests/engine/OnsetDetectorTests.cpp
        tests/engine/BounceTests.cpp
        tests/engine/PadProgramsTests.cpp
        tests/engine/PadGateTests.cpp
        tests/engine/CutProc11Tests.cpp
        tests/engine/CutterTests.cpp
        tests/engine/EffectsTests.cpp
        tests/engine/ImpactEffectsTests.cpp
        tests/engine/FrequencyEffectsTests.cpp
        tests/engine/ImpactIntegrationTests.cpp
        tests/engine/FractureIntegrationTests.cpp
        tests/engine/ResonatorTests.cpp
        tests/engine/RouteRackTests.cpp
        tests/engine/ResonanceIntegrationTests.cpp
        tests/engine/FractureTests.cpp
        tests/engine/LivePlayerTests.cpp
        tests/engine/LoopsTests.cpp
        tests/engine/PhraseSeedTests.cpp
        tests/engine/RandomTests.cpp
        tests/engine/SQPusherTests.cpp
        tests/engine/StraightTests.cpp
        tests/engine/RushTests.cpp
        tests/engine/TapeTests.cpp
        tests/engine/WarpCutTests.cpp)
    target_include_directories(LivecutEngineTests PRIVATE tests)
    hba_build_for_host_only(LivecutEngineTests)
    target_link_libraries(LivecutEngineTests
        PRIVATE
            LivecutEngine
            Catch2::Catch2WithMain)

    if(HBA_RTSAN)
        hba_realtime_suite(LivecutEngineTests
            "[cutter],[effects],[impact],[frequency],[resonator],[fracture],[player],[pad],[padgate],[loops],[rush],[tape],[bounce],[warpcut],[sqpusher],[cutproc11],[history],[onset],[swinggrid],[delaytimeline],[attackcache],[accent],[hold],[route],[recut]")
    else()
        catch_discover_tests(LivecutEngineTests DISCOVERY_MODE PRE_TEST)
    endif()

    if(LIVECUT_BUILD_PLUGIN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME livecut-editor-renders
            COMMAND /usr/bin/python3 "${PROJECT_SOURCE_DIR}/../../factory/release/standalone-linux.py"
                    "$<TARGET_FILE:Livecut_Standalone>")
    endif()
endif()
