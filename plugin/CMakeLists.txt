juce_add_plugin(TractForm
    PRODUCT_NAME "Tract Form"
    DESCRIPTION "Sub-locked neuro bass synthesizer"
    COMPANY_NAME "Human Being Music"
    COMPANY_WEBSITE "https://github.com/seandearnaley/tract-form"
    BUNDLE_ID "com.humanbeingmusic.tractform"
    PLUGIN_MANUFACTURER_CODE Hbmu
    PLUGIN_CODE Trfm
    FORMATS VST3 AU Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
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
target_link_libraries(TractFormCode INTERFACE
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
