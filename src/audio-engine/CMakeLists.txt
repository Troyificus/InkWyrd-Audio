add_library(AudioEngine STATIC
    PlaylistEngine.cpp
    PlaylistLibrary.cpp
    SoundboardEngine.cpp
    SoundboardLayout.cpp
    TrackSettingsStore.cpp
    TrackLibrary.cpp
    NoiseSuppressor.cpp
    SpectrumTap.cpp
    Mp3AudioFormat.cpp
    MediaFoundationAudioFormat.cpp
    PlaylistEngine.h
    PlaylistLibrary.h
    SoundboardEngine.h
    SoundboardLayout.h
    TrackSettingsStore.h
    TrackLibrary.h
    NoiseSuppressor.h
    SpectrumTap.h
    Mp3AudioFormat.h
    MediaFoundationAudioFormat.h
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
    juce::juce_dsp
    juce::juce_audio_formats
    # For MediaFoundationAudioFormat.cpp (AAC/M4A + WMA decode)
    mfplat
    mfreadwrite
    mfuuid
    ole32
)

# PRIVATE: NoiseSuppressor.h deliberately forward-declares DenoiseState
# rather than including rnnoise.h, so nothing downstream needs the
# library's headers on its include path.
target_link_libraries(AudioEngine PRIVATE rnnoise)
