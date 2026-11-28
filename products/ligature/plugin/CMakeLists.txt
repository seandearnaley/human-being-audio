include("${PROJECT_SOURCE_DIR}/../../cmake/StudioProduct.cmake")
hba_product(ligature)

juce_add_plugin(Ligature
    PRODUCT_NAME "${HBA_name}"
    DESCRIPTION "Chords, parts and the bridges between your sections"
    COMPANY_NAME "${HBA_publisher}"
    COMPANY_WEBSITE "${HBA_website}"
    BUNDLE_ID "${HBA_bundle}"
    PLUGIN_MANUFACTURER_CODE "${HBA_manufacturerCode}"
    PLUGIN_CODE "${HBA_pluginCode}"
    FORMATS VST3 AU Standalone
    IS_SYNTH TRUE
    IS_MIDI_EFFECT FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    VST3_CATEGORIES Instrument Generator
    AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
    ICON_BIG "${PROJECT_SOURCE_DIR}/brand/icon/png/icon-1024.png"
    COPY_PLUGIN_AFTER_BUILD ${LG_INSTALL_PLUGIN})

file(GLOB LG_PRESET_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/presets/*.json")
juce_add_binary_data(LigaturePresets HEADER_NAME LigaturePresetsData.h NAMESPACE LigaturePresetsData SOURCES ${LG_PRESET_FILES})

# Compile plugin sources once into Ligature (JUCE's SharedCode static archive) so tests link directly.
target_sources(Ligature PRIVATE
    src/LinkHub.h
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
target_include_directories(Ligature PUBLIC src)
if(TARGET lg_voices)
    target_link_libraries(Ligature PUBLIC lg_voices)
    target_compile_definitions(Ligature PUBLIC LG_HAS_STUDIO_VOICES=1)
endif()
target_include_directories(Ligature SYSTEM INTERFACE "${JUCE_SOURCE_DIR}/modules")

target_compile_definitions(Ligature PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=$<BOOL:${LG_BUILD_TESTS}>)

target_link_libraries(Ligature
    PRIVATE
        hba_plugin_host
        lg_warnings
        LigaturePresets
        juce::juce_audio_utils
    PUBLIC
        hba_studio_ui
        hba_plugin_host_headers
        lg_core
        lg_engine
        lg_ui
        lg_ui_brand
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
