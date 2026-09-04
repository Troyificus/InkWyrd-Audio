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
    ../app/gui/Dialogs.cpp
)

target_include_directories(AudioEngineTest PRIVATE ../app/gui)

target_compile_definitions(AudioEngineTest PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    # Test target only. runDispatchLoopUntil() is what lets a check let a
    # crossfade actually RUN to completion - juce::Timer needs a pumped
    # message loop - and without it every test can only observe a fade
    # starting or being cut short. beta.8 shipped a bug that lives
    # exactly at the moment a fade completes, past 155 passing checks.
    # Deliberately not set for the app, where modal loops are a hazard.
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_ASIO=0
    JUCE_WASAPI=1
)

target_link_libraries(AudioEngineTest PRIVATE
    AudioEngine
    juce::juce_gui_basics
)
