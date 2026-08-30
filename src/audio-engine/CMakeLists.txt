add_library(AudioEngine STATIC
    PlaylistEngine.cpp
    SoundboardEngine.cpp
    PlaylistEngine.h
    SoundboardEngine.h
)

target_include_directories(AudioEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(AudioEngine PUBLIC
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    # WASAPI, not ASIO - see docs/design-brief.md section 7 (Steinberg
    # moved the ASIO SDK to GPLv3; embedding it risks license
    # contamination for a closed-source app). This is JUCE's default on
    # Windows, but set explicitly so it can't be silently reintroduced.
    JUCE_ASIO=0
    JUCE_WASAPI=1
)

target_link_libraries(AudioEngine PUBLIC
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
)
