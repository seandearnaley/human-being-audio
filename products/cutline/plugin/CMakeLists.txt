include("${PROJECT_SOURCE_DIR}/../../cmake/StudioProduct.cmake")
hba_product(cutline)

juce_add_plugin(Cutline
    PRODUCT_NAME "${HBA_name}"
    DESCRIPTION "Expressive phrase grammar, microtiming and MPE/CC MIDI articulator"
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
    COPY_PLUGIN_AFTER_BUILD ${CL_INSTALL_PLUGIN})

file(GLOB CL_PRESET_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/presets/*.json")
juce_add_binary_data(CutlinePresets HEADER_NAME CutlinePresetsData.h NAMESPACE CutlinePresetsData SOURCES ${CL_PRESET_FILES})

add_library(CutlineCode INTERFACE)
target_sources(CutlineCode INTERFACE
    src/CurveStore.cpp
    src/CurveStore.h
    src/MidiExporter.cpp
    src/MidiExporter.h
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
if(APPLE)
    target_sources(CutlineCode INTERFACE src/MacSurface.mm)
endif()
target_include_directories(CutlineCode INTERFACE src)
target_link_libraries(CutlineCode INTERFACE
    hba_studio_ui
    cl_core
    cl_engine
    cl_ui
    cl_ui_brand
    CutlinePresets
    juce::juce_audio_utils)

target_compile_definitions(Cutline PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Cutline
    PRIVATE
        CutlineCode
        cl_warnings
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
