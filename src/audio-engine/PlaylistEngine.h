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

    // Where the playing track is up to, and how long it is, in seconds.
    // Both 0 when nothing is loaded. Read from the ACTIVE deck only:
    // during a crossfade the outgoing deck is still running, and
    // reporting whichever happened to be louder would make the readout
    // jump backwards mid-fade.
    double getCurrentPositionSeconds() const;
    double getCurrentTrackLengthSeconds() const;

    // Jump within the playing track. Clamped to the track's length;
    // ignored when nothing is loaded. Deliberately does NOT touch the
    // crossfade state - seeking during a fade would leave the outgoing
    // deck mid-curve with no way to finish cleanly, so a seek only ever
    // moves the deck the user can actually hear.
    void setPositionSeconds(double seconds);

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

    // The length of the crossfade actually running, so the self-test can
    // assert that a track's own fade length was the one used - not merely
    // that some crossfade started.
    double getActiveCrossfadeSeconds() const { return activeCrossfadeSeconds; }

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

    // How long THIS track takes to fade into whatever follows it, in
    // seconds; 0 means "use the global crossfade length". Asked of the
    // OUTGOING track, because a transition is that track leaving.
    //
    // Per track rather than per pair of tracks: playlists shuffle, so a
    // pairing mostly never comes up, whereas "this one ends on a long
    // tail" is true of the track wherever it lands.
    void setTrackFadeProvider(std::function<double(const juce::File&)> provider);

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

    // A HARD stop, as distinct from pause(): silence now, forget where we
    // were, and let Play begin the list again from the top. pause() keeps
    // your place; this deliberately doesn't.
    void hardStop();

    // Ramps everything down to silence over the given time and then hard
    // stops. Pressing Play (or Stop) during the ramp cancels it.
    //
    // Fades the MUSIC, not the microphone: this is for ending a scene,
    // and fading the host out mid-sentence would be a strange thing for a
    // button next to Stop to do. The master fader is there for taking
    // absolutely everything down.
    void fadeOutAndStop(double seconds);
    bool isFadingOut() const { return fadingOut; }

    // Whether one track fades into the next at all. With this off, a
    // track runs to its end and the next starts immediately.
    void setCrossfadeEnabled(bool shouldCrossfade);
    bool isCrossfadeEnabled() const { return crossfadeEnabled; }

    // Repeat the CURRENT TRACK instead of moving on - for a single
    // looping bed rather than a playlist that starts over. Skip still
    // moves to the next track: looping only governs what happens when a
    // track reaches its own end.
    void setLoopEnabled(bool shouldLoop);
    bool isLoopEnabled() const { return loopEnabled; }

    // Silence between repeats. 0 hands the track over exactly as a normal
    // transition would - so with crossfade on, it dissolves into itself
    // and loops seamlessly.
    static constexpr double kMaxLoopGapSeconds = 10.0;
    void setLoopGapSeconds(double seconds);
    double getLoopGapSeconds() const { return loopGapSeconds; }

    // True while sitting in the silence between repeats.
    bool isWaitingForLoopGap() const { return waitingForLoopGap; }

    static constexpr double kMinCrossfadeSeconds = 0.5;
    static constexpr double kMaxCrossfadeSeconds = 15.0;
    void setCrossfadeSeconds(double seconds);
    double getCrossfadeSeconds() const { return crossfadeSeconds; }

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
    // One place that decides what gain each deck should be at, given the
    // crossfade position, the two tracks' trims and any fade-out in
    // progress. Three separate things multiply here, and having them
    // applied from several places is how they end up fighting.
    void applyDeckGains();
    float gainFor(const juce::File& file) const;

    // How far before the end of a track the next one has to be started.
    double transitionLookAheadSeconds() const;

    // The crossfade length to use when the given track is the one
    // leaving: its own, or the global default if it hasn't got one.
    double fadeSecondsFor(const juce::File& file) const;

    // What the timer does at the end of a track while looping.
    void advanceLooping(Deck& deck, double dt);
    void restartCurrentTrack();

    // True once the current track has finished. Writes how much of it is
    // left into remainingOut along the way, since every caller wants both.
    bool hasReachedEndOfTrack(const Deck& deck, double& remainingOut) const;

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
    bool crossfadeEnabled = true;
    double crossfadeSeconds = 3.0;

    // The length of the fade actually in progress, captured when it
    // started. Changing a setting mid-fade must not make the ramp jump.
    double activeCrossfadeSeconds = 3.0;

    // Whether the user has asked for playback at all. Needed because
    // AudioTransportSource STOPS ITSELF when it reaches the end of a
    // track, so "the deck isn't playing" means either "it finished" or
    // "you pressed Pause" - and those want opposite responses.
    bool playbackRequested = false;

    bool loopEnabled = false;
    double loopGapSeconds = 0.0;
    bool waitingForLoopGap = false;
    double loopGapElapsedSeconds = 0.0;

    bool fadingOut = false;

    // A new list or track was asked for while a fade-out was running.
    // The fade is abandoned, but the level it had reached is folded into
    // the OUTGOING track's gain, so the crossfade that follows carries it
    // down from where it actually is. Simply cancelling the fade would
    // snap the old track back up to full volume for an instant.
    void abandonFadeOutKeepingLevel();
    double fadeOutElapsedSeconds = 0.0;
    double fadeOutSeconds = 5.0;
    float fadeGain = 1.0f; // 1 normally, ramping to 0 during a fade-out
    juce::File currentTrackFile, incomingTrackFile;

    std::function<float(const juce::File&)> trackGainProvider;
    std::function<double(const juce::File&)> trackFadeProvider;

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
