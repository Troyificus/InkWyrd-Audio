juce_add_console_app(AudioEngineTest
    PRODUCT_NAME "Inkwyrd Audio Engine Test"
)

target_sources(AudioEngineTest PRIVATE
    Main.cpp
    # The real PlaylistPanel, so the self-test can drive its Explorer
    # drag-and-drop path directly. A JUCE Component doesn't need to be on
    # screen to be laid out and have filesDropped() called on it, and
    # this keeps the drop logic covered by the same headless run as
    # everything else rather than relying on a manual drag.
    ../app/gui/PlaylistPanel.cpp
    ../app/gui/SoundboardGridComponent.cpp
)

target_include_directories(AudioEngineTest PRIVATE ../app/gui)

target_compile_definitions(AudioEngineTest PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_ASIO=0
    JUCE_WASAPI=1
)

target_link_libraries(AudioEngineTest PRIVATE
    AudioEngine
    juce::juce_gui_basics
)
