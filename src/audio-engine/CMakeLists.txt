add_library(AudioEngine STATIC
    PlaylistEngine.cpp
    PlaylistLibrary.cpp
    SoundboardEngine.cpp
    SoundboardLayout.cpp
    TrackSettingsStore.cpp
    TrackLibrary.cpp
    TrackMetadataStore.cpp
    TagEditor.cpp
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
    TrackMetadataStore.h
    TagEditor.h
    DuckEnvelope.h
    SceneLibrary.cpp
    SceneLibrary.h
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

# TagLib reads AND writes tags for every format this app plays, which is
# what makes the tag editor possible - the Windows property system can
# read them but is no use for writing. Dual-licensed LGPL-2.1 / MPL-1.1
# and linked as a DLL from vcpkg's dynamic triplet; see
# docs/THIRD_PARTY_LICENSES.md.
find_package(taglib CONFIG REQUIRED)

target_link_libraries(AudioEngine PUBLIC
    TagLib::tag
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
    # For TrackMetadataStore's IPropertyStore tag reading.
    propsys
    ole32
)

# PRIVATE: NoiseSuppressor.h deliberately forward-declares DenoiseState
# rather than including rnnoise.h, so nothing downstream needs the
# library's headers on its include path.
target_link_libraries(AudioEngine PRIVATE rnnoise)
