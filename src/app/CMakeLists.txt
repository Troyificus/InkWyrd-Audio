# The publisher fields end up in the exe's version resource (right-click ->
# Properties -> Details). Left unset, JUCE fills CompanyName with its
# placeholder "yourcompany", and a program with placeholder publisher
# details and no icon is one more thing for antivirus heuristics to
# dislike about an unsigned download. Keep COMPANY_NAME matching
# MyAppPublisher in installer/InkwyrdAudio.iss.
juce_add_gui_app(InkwyrdAudioApp
    PRODUCT_NAME "Inkwyrd Audio"
    COMPANY_NAME "Troy"
    COMPANY_COPYRIGHT "Copyright (c) 2026 Troy"
    COMPANY_WEBSITE "https://github.com/Troyificus/InkWyrd-Audio"
    DESCRIPTION "Plays music and sound effects into a Discord voice channel"
    # Rendered from InkwyrdLookAndFeel::drawLogo() by the test harness's
    # INKWYRD_ICONRENDER mode - see installer/make-icon.ps1.
    ICON_BIG "${CMAKE_SOURCE_DIR}/assets/icon/icon-256.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/assets/icon/icon-32.png"
)

target_sources(InkwyrdAudioApp PRIVATE
    Main.cpp
    MasterEngine.cpp
    DiscordAudioSender.cpp
    DiscordRpcClient.cpp
    ControlServer.cpp
    UpdateCheck.cpp
    UpdateCheck.h
    MessageThreadWatchdog.cpp
    MessageThreadWatchdog.h
    MasterEngine.h
    DiscordAudioSender.h
    DiscordRpcClient.h
    ControlServer.h
    gui/InkwyrdAudioApplication.cpp
    gui/InkwyrdAudioApplication.h
    gui/MainWindow.cpp
    gui/MainWindow.h
    gui/DetachableWindow.cpp
    gui/DetachableWindow.h
    gui/InkwyrdLookAndFeel.cpp
    gui/InkwyrdLookAndFeel.h
    gui/InkwyrdTheme.h
    gui/NowPlayingDisplay.cpp
    gui/NowPlayingDisplay.h
    gui/WindowLayoutStore.cpp
    gui/WindowLayoutStore.h
    gui/WindowSnapping.cpp
    gui/WindowSnapping.h
    gui/SetupComponent.cpp
    gui/SetupComponent.h
    gui/PlayerWindow.cpp
    gui/PlayerWindow.h
    gui/PlayerComponent.cpp
    gui/PlayerComponent.h
    gui/PlaylistWindow.cpp
    gui/PlaylistWindow.h
    gui/PlaylistTrackListComponent.cpp
    gui/PlaylistTrackListComponent.h
    gui/LibraryWindow.cpp
    gui/LibraryWindow.h
    gui/LibraryFolderTree.cpp
    gui/LibraryFolderTree.h
    gui/PlaylistPanel.cpp
    gui/PlaylistPanel.h
    gui/SoundboardWindow.cpp
    gui/SoundboardWindow.h
    gui/SoundboardGridComponent.cpp
    gui/SoundboardGridComponent.h
    gui/VoiceFxWindow.cpp
    gui/VoiceFxWindow.h
    gui/VoiceFxComponent.cpp
    gui/VoiceFxComponent.h
    gui/PluginEditorWindow.cpp
    gui/PluginEditorWindow.h
    gui/AppSettings.cpp
    gui/AppSettings.h
    gui/SkinLoader.cpp
    gui/SkinLoader.h
    gui/TrackSearch.h
    gui/TagEditorWindow.cpp
    gui/TagEditorWindow.h
    gui/Dialogs.cpp
    gui/Dialogs.h
    gui/VolumeCallout.h
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
    INKWYRD_VERSION_STRING="${INKWYRD_VERSION_STRING}"
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
