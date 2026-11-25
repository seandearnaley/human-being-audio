# Self-contained studio product metadata for Strike Body v1 test build
# (matches cmake/StudioProduct.cmake without modifying studio.json yet).
set(root "${PROJECT_SOURCE_DIR}/../..")
set(slug "strike-body")
set(HBA_publisher "Human Being Audio")
set(HBA_website "https://humanbeingaudio.com")
set(HBA_repository "https://github.com/seandearnaley/human-being-audio")
set(HBA_name "Strike Body")
set(HBA_bundle "com.humanbeingmusic.strikebody")
set(HBA_manufacturerCode "Hbmu")
set(HBA_pluginCode "Stbd")
set(HBA_license "AGPL-3.0-or-later")

configure_file("${root}/modules/studio-ui/Product.h.in" "studio/Product.h" @ONLY)
juce_add_binary_data(hba_studio_art HEADER_NAME HbaStudioArt.h NAMESPACE HbaStudioArt
    SOURCES "${root}/design/studio/mark.png")
add_library(hba_studio_ui INTERFACE)
target_link_libraries(hba_studio_ui INTERFACE hba_studio_art)
target_include_directories(hba_studio_ui INTERFACE "${root}/modules/studio-ui" "${CMAKE_CURRENT_BINARY_DIR}")

juce_add_plugin(StrikeBody
    PRODUCT_NAME "${HBA_name}"
    DESCRIPTION "Phase-locked neuro snare, kick & nonlinear modal percussion synthesizer"
    COMPANY_NAME "${HBA_publisher}"
    COMPANY_WEBSITE "${HBA_website}"
    BUNDLE_ID "${HBA_bundle}"
    PLUGIN_MANUFACTURER_CODE "${HBA_manufacturerCode}"
    PLUGIN_CODE "${HBA_pluginCode}"
    FORMATS VST3 AU Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Drum Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    ICON_BIG "${PROJECT_SOURCE_DIR}/brand/icon/png/icon-1024.png"
    COPY_PLUGIN_AFTER_BUILD ${SB_INSTALL_PLUGIN})

file(GLOB SB_PRESET_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/presets/*.json")
juce_add_binary_data(StrikeBodyPresets HEADER_NAME StrikeBodyPresetsData.h NAMESPACE StrikeBodyPresetsData
    SOURCES ${SB_PRESET_FILES})

target_sources(StrikeBody PRIVATE
    src/Bouncer.cpp
    src/Bouncer.h
    src/Parameters.cpp
    src/Parameters.h
    src/PluginEditor.cpp
    src/PluginEditor.h
    src/PluginModel.cpp
    src/PluginModel.h
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/Presets.cpp
    src/Presets.h
    src/Telemetry.h)

target_include_directories(StrikeBody PUBLIC src)
target_include_directories(StrikeBody SYSTEM INTERFACE "${JUCE_SOURCE_DIR}/modules")

target_compile_definitions(StrikeBody PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=$<BOOL:${SB_BUILD_TESTS}>)

target_link_libraries(StrikeBody
    PRIVATE
        hba_plugin_host
        sb_warnings
        StrikeBodyPresets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        hba_studio_ui
        hba_plugin_host_headers
        sb_core
        sb_engine
        sb_ui
        sb_ui_brand
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
