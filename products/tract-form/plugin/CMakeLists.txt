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

# The plugin's own sources live in an INTERFACE library so the tests and the
# snapshot tool compile the same code without a plugin wrapper.
add_library(TractFormCode INTERFACE)
target_sources(TractFormCode INTERFACE
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
target_include_directories(TractFormCode INTERFACE src)
target_link_libraries(TractFormCode INTERFACE hba_studio_ui
    tf_analysis
    tf_core
    tf_engine
    tf_ui
    TractFormPresets
    juce::juce_audio_utils
    juce::juce_dsp)

target_compile_definitions(TractForm PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(TractForm
    PRIVATE
        TractFormCode
        tf_warnings
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# Headless full-stack snapshot: real processor, real analysis, real interface.
add_executable(tf-plugin-snapshot tools/snapshot.cpp)
# The JucePlugin_* definitions only exist on the plugin target, so copy them.
target_compile_definitions(tf-plugin-snapshot PRIVATE $<TARGET_PROPERTY:TractForm,COMPILE_DEFINITIONS>)
target_link_libraries(tf-plugin-snapshot PRIVATE TractFormCode tf_warnings)
