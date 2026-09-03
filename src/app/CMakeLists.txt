juce_add_gui_app(InkwyrdAudioApp
    PRODUCT_NAME "Inkwyrd Audio"
)

target_sources(InkwyrdAudioApp PRIVATE
    Main.cpp
    MasterEngine.cpp
    DiscordAudioSender.cpp
    ControlServer.cpp
    MessageThreadWatchdog.cpp
    MessageThreadWatchdog.h
    MasterEngine.h
    DiscordAudioSender.h
    ControlServer.h
    gui/InkwyrdAudioApplication.cpp
    gui/InkwyrdAudioApplication.h
    gui/MainWindow.cpp
    gui/MainWindow.h
    gui/SetupComponent.cpp
    gui/SetupComponent.h
    gui/PlayerComponent.cpp
    gui/PlayerComponent.h
    gui/PlaylistPanel.cpp
    gui/PlaylistPanel.h
    gui/SoundboardGridComponent.cpp
    gui/SoundboardGridComponent.h
    gui/VoiceFxComponent.cpp
    gui/VoiceFxComponent.h
    gui/AppSettings.cpp
    gui/AppSettings.h
    gui/Dialogs.cpp
    gui/Dialogs.h
    gui/DiscordConnector.cpp
    gui/DiscordConnector.h
)

# So gui/*.cpp (in a subdirectory) can #include "MasterEngine.h" etc.
# unqualified, same as Main.cpp always could when everything lived
# directly in this directory.
target_include_directories(InkwyrdAudioApp PRIVATE .)

target_compile_definitions(InkwyrdAudioApp PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:InkwyrdAudioApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:InkwyrdAudioApp,JUCE_VERSION>"
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
    juce::juce_gui_basics
)

inkwyrd_copy_dave_dll(InkwyrdAudioApp)
