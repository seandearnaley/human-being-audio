include("${PROJECT_SOURCE_DIR}/../../cmake/StudioProduct.cmake")
hba_product(tract-form)

juce_add_plugin(TractForm
    PRODUCT_NAME "${HBA_name}"
    DESCRIPTION "Sub-locked neuro bass synthesizer"
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
    COPY_PLUGIN_AFTER_BUILD ${TF_INSTALL_PLUGIN})

# Factory presets are embedded in the binary.
file(GLOB TF_PRESET_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/presets/*.json")
juce_add_binary_data(TractFormPresets SOURCES ${TF_PRESET_FILES})

# Compile plugin sources once into TractForm (JUCE's SharedCode static archive)
# so tests and snapshot tools link directly instead of recompiling plugin & JUCE.
target_sources(TractForm PRIVATE
    src/Analysis.cpp
    src/Analysis.h
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
    src/Resampler.cpp
    src/Resampler.h)
target_include_directories(TractForm PUBLIC src)
target_include_directories(TractForm SYSTEM INTERFACE "${JUCE_SOURCE_DIR}/modules")

target_compile_definitions(TractForm PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=$<BOOL:${TF_BUILD_TESTS}>)

target_link_libraries(TractForm
    PRIVATE
        hba_plugin_host
        tf_warnings
        TractFormPresets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        hba_studio_ui
        hba_plugin_host_headers
        tf_analysis
        tf_core
        tf_engine
        tf_ui
        tf_ui_brand
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# Headless full-stack snapshot: real processor, real analysis, real interface.
add_executable(tf-plugin-snapshot tools/snapshot.cpp)
hba_build_for_host_only(tf-plugin-snapshot)
target_link_libraries(tf-plugin-snapshot PRIVATE TractForm tf_warnings)
