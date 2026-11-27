include("${PROJECT_SOURCE_DIR}/../../cmake/StudioProduct.cmake")
hba_product(prism-vein)

juce_add_plugin(PrismVein
    PRODUCT_NAME "${HBA_name}"
    DESCRIPTION "Phase-locked spectral colorizer, MIDI chord vocoder and Bark auto-tame resonator"
    COMPANY_NAME "${HBA_publisher}"
    COMPANY_WEBSITE "${HBA_website}"
    BUNDLE_ID "${HBA_bundle}"
    PLUGIN_MANUFACTURER_CODE "${HBA_manufacturerCode}"
    PLUGIN_CODE "${HBA_pluginCode}"
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Filter
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Prism Vein Standalone processes live audio input."
    ICON_BIG "${PROJECT_SOURCE_DIR}/brand/icon/png/icon-1024.png"
    COPY_PLUGIN_AFTER_BUILD ${PV_INSTALL_PLUGIN})

file(GLOB PV_PRESET_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/presets/*.json")
juce_add_binary_data(PrismVeinPresets HEADER_NAME PrismVeinPresetsData.h NAMESPACE PrismVeinPresetsData SOURCES ${PV_PRESET_FILES})

target_sources(PrismVein PRIVATE
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
    src/StateStore.cpp
    src/StateStore.h
    src/Telemetry.h)

target_include_directories(PrismVein PUBLIC src)
target_include_directories(PrismVein SYSTEM INTERFACE "${JUCE_SOURCE_DIR}/modules")

target_compile_definitions(PrismVein PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=$<BOOL:${PV_BUILD_TESTS}>)

target_link_libraries(PrismVein
    PRIVATE
        hba_plugin_host
        pv_warnings
        PrismVeinPresets
        juce::juce_audio_utils
    PUBLIC
        hba_studio_ui
        hba_plugin_host_headers
        pv_core
        pv_engine
        pv_ui
        pv_ui_brand
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
