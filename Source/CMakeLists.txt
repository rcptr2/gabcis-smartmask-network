add_library(SmartMaskCore STATIC
    SmartMaskRegistry.cpp
    SmartMaskRegistry.h
    SpectralEngine.cpp
    SpectralEngine.h
    MaskingProcessor.cpp
    MaskingProcessor.h
    PluginProcessor.cpp
    PluginProcessor.h
    SpectrumVisualizerComponent.cpp
    SpectrumVisualizerComponent.h
    PriorityListComponent.cpp
    PriorityListComponent.h
    PluginEditor.cpp
    PluginEditor.h
)

target_include_directories(SmartMaskCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(SmartMaskCore PUBLIC cxx_std_20)
target_link_libraries(SmartMaskCore PUBLIC juce::juce_dsp juce::juce_audio_processors)

if(SMARTMASK_ENABLE_TSAN)
    target_compile_options(SmartMaskCore PUBLIC -fsanitize=thread -g -O1)
    target_link_options(SmartMaskCore PUBLIC -fsanitize=thread)
endif()

# The actual pluggable VST3/AU/Standalone binary. All real code (including
# the createPluginFilter() factory function JUCE's plugin wrappers need)
# lives in SmartMaskCore above, which is what SmartMaskRegistryTests /
# SpectralEngineTests / MaskingProcessorTests / PluginProcessorTests link
# directly -- this target just adds the JUCE plugin-format wrapper shell
# around that same code.
juce_add_plugin(SmartMaskNetworkPlugin
    COMPANY_NAME "Gabci Audio"
    BUNDLE_ID "com.gabciaudio.smartmasknetwork"
    PLUGIN_MANUFACTURER_CODE Gabc
    PLUGIN_CODE Smsk
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "SmartMask Network"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_Effect
)

target_link_libraries(SmartMaskNetworkPlugin
    PRIVATE
        SmartMaskCore
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# This is a brand-new plugin with no prior VST2 or VST3 release to stay
# parameter-automation-compatible with, so JUCE's VST2/VST3 ID-migration
# guard (juce_audio_plugin_client_VST3.cpp) doesn't apply -- disabling
# JUCE_VST3_CAN_REPLACE_VST2 is exactly the case the guard's own comment
# recommends for "released neither a VST2 or VST3 version yet".
target_compile_definitions(SmartMaskNetworkPlugin PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
)
