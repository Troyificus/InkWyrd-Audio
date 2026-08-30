add_library(VstHosting STATIC
    PluginScanner.cpp
    PluginChain.cpp
    PluginScanner.h
    PluginChain.h
)

target_include_directories(VstHosting PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(VstHosting PUBLIC
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_ASIO=0
    JUCE_WASAPI=1
    # VST3 only, per docs/design-brief.md section 6 - no VST2 (the SDK
    # for that is no longer redistributable the way VST3's is) and no
    # other plugin format.
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
)

target_link_libraries(VstHosting PUBLIC
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
)
