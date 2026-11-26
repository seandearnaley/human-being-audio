include("${PROJECT_SOURCE_DIR}/../../cmake/StudioProduct.cmake")
hba_product(torus-glide)

juce_add_plugin(TorusGlide
    PRODUCT_NAME "${HBA_name}"
    DESCRIPTION "Continuous pitch-manifold synthesizer: bar-locked Shepard bass, polyphonic chord-bend copedent and Mobius glides"
    COMPANY_NAME "${HBA_publisher}"
    COMPANY_WEBSITE "${HBA_website}"
    BUNDLE_ID "${HBA_bundle}"
    PLUGIN_MANUFACTURER_CODE "${HBA_manufacturerCode}"
    PLUGIN_CODE "${HBA_pluginCode}"
    FORMATS VST3 AU Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    ICON_BIG "${PROJECT_SOURCE_DIR}/brand/icon/png/icon-1024.png"
    COPY_PLUGIN_AFTER_BUILD ${TG_INSTALL_PLUGIN})

file(GLOB TG_PRESET_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/presets/*.json")
juce_add_binary_data(TorusGlidePresets HEADER_NAME TorusGlidePresetsData.h NAMESPACE TorusGlidePresetsData
    SOURCES ${TG_PRESET_FILES})

target_sources(TorusGlide PRIVATE
    src/CurveStore.cpp
    src/CurveStore.h
    src/MatrixStore.cpp
    src/MatrixStore.h
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
    src/Resampler.cpp
    src/Resampler.h
    src/Telemetry.h)

target_include_directories(TorusGlide PUBLIC src)
target_include_directories(TorusGlide SYSTEM INTERFACE "${JUCE_SOURCE_DIR}/modules")

target_compile_definitions(TorusGlide PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(TorusGlide
    PRIVATE
        hba_plugin_host
        tg_warnings
        TorusGlidePresets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        hba_studio_ui
        hba_plugin_host_headers
        tg_core
        tg_engine
        tg_ui
        tg_ui_brand
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
