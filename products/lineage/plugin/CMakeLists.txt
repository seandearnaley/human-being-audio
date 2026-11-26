include("${PROJECT_SOURCE_DIR}/../../cmake/StudioProduct.cmake")
hba_product(lineage)

juce_add_plugin(Lineage
    PRODUCT_NAME "${HBA_name}"
    DESCRIPTION "Non-destructive resampling Reese and bass sculpting synthesizer with live multi-generation Strata and phase-locked mono sub"
    COMPANY_NAME "${HBA_publisher}"
    COMPANY_WEBSITE "${HBA_website}"
    BUNDLE_ID "${HBA_bundle}"
    PLUGIN_MANUFACTURER_CODE "${HBA_manufacturerCode}"
    PLUGIN_CODE "${HBA_pluginCode}"
    FORMATS VST3 AU Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    ICON_BIG "${PROJECT_SOURCE_DIR}/brand/icon/png/icon-1024.png"
    COPY_PLUGIN_AFTER_BUILD ${LN_INSTALL_PLUGIN})

file(GLOB LN_PRESET_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/presets/*.json")
juce_add_binary_data(LineagePresets HEADER_NAME LineagePresetsData.h NAMESPACE LineagePresetsData
    SOURCES ${LN_PRESET_FILES})

target_sources(Lineage PRIVATE
    src/CurveStore.cpp
    src/CurveStore.h
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
    src/PrintStore.cpp
    src/PrintStore.h
    src/Resampler.cpp
    src/Resampler.h
    src/Telemetry.h)

target_include_directories(Lineage PUBLIC src)
target_include_directories(Lineage SYSTEM INTERFACE "${JUCE_SOURCE_DIR}/modules")

target_compile_definitions(Lineage PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(Lineage
    PRIVATE
        hba_plugin_host
        ln_warnings
        LineagePresets
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        hba_studio_ui
        hba_plugin_host_headers
        ln_core
        ln_engine
        ln_ui
        ln_ui_brand
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
