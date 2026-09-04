#pragma once

#include <functional>

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

    // Replaces the play order outright. Deliberately has NO playback side
    // effects: editing the list you're currently listening to shouldn't
    // restart or interrupt the track that's playing. Callers decide
    // separately whether to start() or crossfadeToTracks().
    void setTracks(const juce::Array<juce::File>& tracks);

    // Re-applies an EDITED version of the list that's already playing -
    // e.g. after files are dropped into the active playlist, or a linked
    // folder is re-scanned. Unlike setTracks() this keeps the play
    // position: tracks that are gone are dropped, new ones are spliced
    // into the part of the order that hasn't played yet, and what's left
    // to play this cycle is otherwise left exactly as it was.
    //
    // setTracks() deliberately resets to the top of the order and
    // reshuffles, which is right for a deliberate switch to a different
    // list and wrong for an in-place edit - adding one track at the end
    // of the list you're listening to shouldn't send it back to track 1.
    void updateTracksPreservingOrder(const juce::Array<juce::File>& tracks);

    // Switches to a different list AND crossfades into it from whatever
    // is playing, using the same equal-power fade as an end-of-track
    // transition. startFrom picks the track to land on ({} = first in
    // order). Safe to call mid-crossfade. No-op on an empty list, so a
    // mis-click can't leave the room in silence.
    void crossfadeToTracks(const juce::Array<juce::File>& tracks, const juce::File& startFrom = {});

    // Jump to a track already in the current list, crossfading into it.
    // Playback continues sequentially from there.
    void crossfadeToTrackInCurrentList(const juce::File& file);

    // How many playable files the current list actually has. Callers use
    // this to tell the user when a chosen folder yielded nothing, rather
    // than sitting silently producing no audio.
    int getNumTracks() const { return playOrder.size(); }

    juce::File getCurrentTrackFile() const { return currentTrackFile; }

    // The resolved play order, and how far through it playback has got.
    // Read-only. Mainly here so the ordering guarantees above can actually
    // be asserted in the self-test - "it didn't crash" is not evidence
    // that an in-place edit preserved the order.
    const juce::Array<juce::File>& getPlayOrder() const { return playOrder; }
    int getNextOrderIndex() const { return nextOrderIndex; }

    // The trim currently in force for the playing track, for the same
    // reason as the two above: so the self-test can assert that a track's
    // volume actually reached the deck, rather than that nothing crashed.
    float getCurrentTrackGain() const { return currentTrackGain; }

    // Fires on the message thread when shuffle actually changes value -
    // including via ControlServer's toggleShuffle, which is why the app
    // can't just watch its own button. Used to persist per-playlist
    // shuffle state. Must not call back into this engine.
    void setShuffleChangedCallback(std::function<void(bool)> callback);

    // Per-track volume trim. The engine asks this for a LINEAR gain each
    // time it loads a track, and folds the answer into the deck gain
    // alongside the crossfade curve - so a track's trim and the fade
    // multiply rather than one overwriting the other.
    //
    // A callback rather than a stored map so the engine doesn't need to
    // know where trims live or when they change; call
    // refreshTrackGains() after editing one that's already playing.
    void setTrackGainProvider(std::function<float(const juce::File&)> provider);
    void refreshTrackGains();

    void setShuffle(bool shouldShuffle);
    bool isShuffleEnabled() const { return shuffleEnabled; }

    // Only valid from a stopped or freshly-constructed state. To change
    // lists while audio is playing use crossfadeToTracks() - start()
    // deliberately stops everything first, so calling it mid-playback
    // cuts rather than fades.
    void start();
    void stop();

    // Stop/resume without losing your place, for the transport's
    // Play/Stop button. stop() is a full teardown used at shutdown and
    // when switching lists; pause() keeps the current track and position
    // so resume() carries on from exactly where it left off.
    void pause();
    void resume();
    bool isPlaying() const { return isAnyDeckPlaying(); }

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
    void beginCrossfadeTo(const juce::File& file);
    void finishCrossfadeNow();
    bool isAnyDeckPlaying() const;
    void seekOrderTo(const juce::File& file);
    void applyCrossfadeGains();
    float gainFor(const juce::File& file) const;

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

    std::function<float(const juce::File&)> trackGainProvider;

    // The trims currently in force for the playing and incoming tracks,
    // cached so applyCrossfadeGains() doesn't have to call out to the
    // provider on every timer tick.
    float currentTrackGain = 1.0f;
    float incomingTrackGain = 1.0f;

    bool shuffleEnabled = true;
    juce::Random random;
    std::function<void(bool)> shuffleChangedCallback;

    double currentSampleRate = 44100.0;
    bool prepared = false;
};
