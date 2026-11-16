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

target_sources(TractForm PRIVATE
    src/Parameters.cpp
    src/Parameters.h
    src/PluginEditor.cpp
    src/PluginEditor.h
    src/PluginProcessor.cpp
    src/PluginProcessor.h)

target_compile_definitions(TractForm PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(TractForm
    PRIVATE
        tf_core
        tf_ui
        tf_warnings
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
