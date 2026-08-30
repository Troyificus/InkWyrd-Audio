juce_add_console_app(VstHostingTest
    PRODUCT_NAME "Inkwyrd VST3 Hosting Test"
)

target_sources(VstHostingTest PRIVATE
    Main.cpp
)

target_compile_definitions(VstHostingTest PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_ASIO=0
    JUCE_WASAPI=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
)

target_link_libraries(VstHostingTest PRIVATE
    VstHosting
    juce::juce_audio_utils
)
