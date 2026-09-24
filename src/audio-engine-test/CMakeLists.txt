juce_add_console_app(AudioEngineTest
    PRODUCT_NAME "Inkwyrd Audio Engine Test"
)

target_sources(AudioEngineTest PRIVATE
    Main.cpp
    # INKWYRD_RESIZETEST: its own file so windows.h stays out of Main.cpp.
    ResizeFlashTest.cpp
    # The real PlaylistPanel, so the self-test can drive its Explorer
    # drag-and-drop path directly. A JUCE Component doesn't need to be on
    # screen to be laid out and have filesDropped() called on it, and
    # this keeps the drop logic covered by the same headless run as
    # everything else rather than relying on a manual drag.
    ../app/gui/PlaylistPanel.cpp
    ../app/gui/PlaylistTrackListComponent.cpp
    # buildFolderTree() is pure grouping over paths - covered headlessly
    # here rather than only by opening the window and looking.
    ../app/gui/LibraryFolderTree.cpp
    # Skin parsing: pure over juce::var, so the rules a skin author will
    # actually hit (missing keys, typos, a newer schema) are covered here
    # rather than by opening Settings and looking.
    ../app/gui/SkinLoader.cpp
    ../app/gui/SkinSprites.cpp
    ../app/gui/ExampleSkins.cpp
    ../app/gui/SoundboardGridComponent.cpp
    ../app/gui/Dialogs.cpp
    # Pure geometry, no Component/peer needed - covers the magnetic-snap
    # math headlessly rather than only by manual dragging.
    ../app/gui/WindowSnapping.cpp
    # For INKWYRD_ICONRENDER, which draws the app icon with the same
    # drawLogo() the title bars use.
    ../app/gui/InkwyrdLookAndFeel.cpp
    # INKWYRD_SCENESNAPSHOT draws these off-screen, so new UI can be
    # looked at without launching the app or driving the mouse.
    ../app/gui/ScenesComponent.cpp
    ../app/gui/SceneEditor.cpp
    # For deriveApplicationId(), which is a pure function over a string
    # and the one part of the Discord auto-mute path that can be checked
    # without a running Discord client.
    ../app/DiscordRpcClient.cpp
    # parseReleasesJson() and isNewerRelease() are pure over a string;
    # the network half of this file is never called from the test.
    ../app/UpdateCheck.cpp
)

target_include_directories(AudioEngineTest PRIVATE ../app/gui ../app)

target_compile_definitions(AudioEngineTest PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    INKWYRD_VERSION_STRING="${INKWYRD_VERSION_STRING}"
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
    comctl32
    AudioEngine
    juce::juce_gui_basics
)
