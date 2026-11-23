include("${PROJECT_SOURCE_DIR}/../../cmake/StudioProduct.cmake")
hba_product(pole-field)

juce_add_plugin(PoleField
    PRODUCT_NAME "${HBA_name}"
    DESCRIPTION "14-pole Z-plane morphing filterbank"
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
    MICROPHONE_PERMISSION_TEXT "Pole Field Standalone processes live audio input."
    ICON_BIG "${PROJECT_SOURCE_DIR}/brand/icon/png/icon-1024.png"
    COPY_PLUGIN_AFTER_BUILD ${PF_INSTALL_PLUGIN})

file(GLOB PF_PRESET_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/presets/*.json")
juce_add_binary_data(PoleFieldPresets HEADER_NAME PoleFieldPresetsData.h NAMESPACE PoleFieldPresetsData SOURCES ${PF_PRESET_FILES})

# Compile plugin sources once into PoleField (JUCE's SharedCode static archive)
# so tests link directly instead of recompiling plugin & JUCE.
target_sources(PoleField PRIVATE
    src/Analysis.cpp
    src/Analysis.h
    src/CurveStore.cpp
    src/CurveStore.h
    src/ImprintStore.cpp
    src/ImprintStore.h
    src/Parameters.cpp
    src/Parameters.h
    src/PluginEditor.cpp
    src/PluginEditor.h
    src/PluginModel.cpp
    src/PluginModel.h
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/Presets.cpp
    src/Presets.h)
target_include_directories(PoleField PUBLIC src)
target_include_directories(PoleField SYSTEM INTERFACE "${JUCE_SOURCE_DIR}/modules")

target_compile_definitions(PoleField PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=$<BOOL:${PF_BUILD_TESTS}>)

target_link_libraries(PoleField
    PRIVATE
        hba_plugin_host
        pf_warnings
        PoleFieldPresets
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        hba_studio_ui
        hba_plugin_host_headers
        pf_core
        pf_engine
        pf_ui
        pf_ui_brand
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
