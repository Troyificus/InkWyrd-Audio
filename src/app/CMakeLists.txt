juce_add_console_app(InkwyrdAudioApp
    PRODUCT_NAME "Inkwyrd Audio"
)

target_sources(InkwyrdAudioApp PRIVATE
    Main.cpp
    MasterEngine.cpp
    DiscordAudioSender.cpp
    MasterEngine.h
    DiscordAudioSender.h
)

target_compile_definitions(InkwyrdAudioApp PRIVATE
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

target_link_libraries(InkwyrdAudioApp PRIVATE
    DiscordVoice
    AudioEngine
    VstHosting
    juce::juce_audio_utils
)

inkwyrd_copy_dave_dll(InkwyrdAudioApp)
