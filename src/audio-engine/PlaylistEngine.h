#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

// Shuffle + crossfade playlist. Two AudioTransportSources are kept alive
// at once so the outgoing and incoming track can briefly overlap - the
// approach the design brief specifies - summed by an internal
// MixerAudioSource that's what callers actually plug into a master mix.
//
// Crossfade timing runs on a juce::Timer, so - like any JUCE Timer -
// this only works while an actual JUCE message loop is being pumped,
// and every public method here (other than the AudioSource overrides,
// which the audio thread calls) should be called from that same message
// thread to avoid racing the timer callback. A bare console app doesn't
// run a message loop by default; see audio-engine-test/Main.cpp for how
// to drive one without a full GUI.
class PlaylistEngine : public juce::AudioSource, private juce::Timer
{
public:
    explicit PlaylistEngine(juce::AudioFormatManager& formatManagerToUse);
    ~PlaylistEngine() override;

    // Scans a folder *recursively* for files the format manager can
    // read and builds a fresh (optionally shuffled) play order from them.
    void loadFolder(const juce::File& folder);

    // How many playable files loadFolder() actually found. Callers use
    // this to tell the user when a chosen folder yielded nothing, rather
    // than sitting silently producing no audio.
    int getNumTracks() const { return playOrder.size(); }

    void setShuffle(bool shouldShuffle);
    bool isShuffleEnabled() const { return shuffleEnabled; }

    void start();
    void stop();

    // Manually begins a crossfade to the next track, same as what
    // happens automatically near the end of the current one.
    void skipToNext();

    juce::String getCurrentTrackName() const;
    bool isCrossfading() const { return crossfading; }

    // juce::AudioSource
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

private:
    struct Deck
    {
        std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
        juce::AudioTransportSource transport;
    };

    void timerCallback() override;
    void loadIntoDeck(Deck& deck, const juce::File& file);
    juce::File pickNextFile();
    void beginCrossfade();
    void applyCrossfadeGains();

    juce::AudioFormatManager& formatManager;
    juce::Array<juce::File> playOrder;
    int nextOrderIndex = 0;

    // Without a background read-ahead thread, AudioTransportSource reads
    // disk blocks synchronously on whatever thread calls getNextAudioBlock
    // - for a live audio callback, that's an audible glitch risk.
    juce::TimeSliceThread readAheadThread { "PlaylistEngine read-ahead" };

    Deck decks[2];
    int activeDeck = 0; // index into decks[] of the currently "main" track
    juce::MixerAudioSource mixer;

    bool crossfading = false;
    double crossfadeElapsedSeconds = 0.0;
    const double crossfadeDurationSeconds = 3.0;
    juce::File currentTrackFile, incomingTrackFile;

    bool shuffleEnabled = true;
    juce::Random random;

    double currentSampleRate = 44100.0;
    bool prepared = false;
};
