#include "PlaylistEngine.h"
#include "PlaylistLibrary.h"

#include <cmath>

namespace
{
    constexpr int kReadAheadBufferSamples = 32768;
    constexpr int kTimerIntervalMs = 30;
}

PlaylistEngine::PlaylistEngine(juce::AudioFormatManager& formatManagerToUse)
    : formatManager(formatManagerToUse)
{
    // Above normal priority: this thread refills the streaming buffers
    // ahead of the real-time audio callback, and losing the CPU to
    // ordinary background work is exactly what produces dropouts.
    readAheadThread.startThread(juce::Thread::Priority::high);
    mixer.addInputSource(&decks[0].transport, false);
    mixer.addInputSource(&decks[1].transport, false);
}

PlaylistEngine::~PlaylistEngine()
{
    stopTimer();
    mixer.removeAllInputs();
    decks[0].transport.setSource(nullptr);
    decks[1].transport.setSource(nullptr);
    readAheadThread.stopThread(2000);
}

void PlaylistEngine::loadFolder(const juce::File& folder)
{
    // Recursive: people point this at an album or library folder whose
    // audio lives a level or two down. Real beta report - the chosen
    // folder held only cover art at the top level and 26 tracks in
    // per-track subfolders, so nothing played and nothing said why.
    setTracks(inkwyrd::scanFolderForAudio(folder, formatManager, true));
}

void PlaylistEngine::setTracks(const juce::Array<juce::File>& tracks)
{
    playOrder = tracks;
    nextOrderIndex = 0;

    if (shuffleEnabled)
        for (int i = playOrder.size() - 1; i > 0; --i)
            playOrder.swap(i, random.nextInt(i + 1));
}

void PlaylistEngine::updateTracksPreservingOrder(const juce::Array<juce::File>& tracks)
{
    // With shuffle off the list's own order IS the play order, so take it
    // outright and simply resume after whatever is playing right now.
    if (!shuffleEnabled)
    {
        playOrder = tracks;

        auto index = playOrder.indexOf(currentTrackFile);
        nextOrderIndex = index >= 0 ? index + 1
                                     : juce::jmin(nextOrderIndex, playOrder.size());
        return;
    }

    // Shuffled: the existing order is a permutation we must NOT rebuild -
    // doing so would re-randomise what's still to come and could replay
    // tracks already heard this cycle. Keep the surviving entries in place
    // and splice the new ones into the not-yet-played remainder, so a
    // track dropped in mid-session can still come up this time round.
    juce::Array<juce::File> retained;
    int survivingBeforeCursor = 0;

    for (int i = 0; i < playOrder.size(); ++i)
    {
        if (!tracks.contains(playOrder[i]))
            continue; // gone from the playlist - drop it

        if (i < nextOrderIndex)
            ++survivingBeforeCursor;

        retained.add(playOrder[i]);
    }

    juce::Array<juce::File> additions;
    for (const auto& file : tracks)
        if (!retained.contains(file))
            additions.add(file);

    playOrder = std::move(retained);
    nextOrderIndex = juce::jmin(survivingBeforeCursor, playOrder.size());

    for (const auto& file : additions)
    {
        auto span = playOrder.size() - nextOrderIndex; // never negative
        playOrder.insert(nextOrderIndex + random.nextInt(span + 1), file);
    }
}

void PlaylistEngine::setTrackGainProvider(std::function<float(const juce::File&)> provider)
{
    trackGainProvider = std::move(provider);
    refreshTrackGains();
}

void PlaylistEngine::setTrackFadeProvider(std::function<double(const juce::File&)> provider)
{
    trackFadeProvider = std::move(provider);
}

double PlaylistEngine::fadeSecondsFor(const juce::File& file) const
{
    if (file == juce::File() || trackFadeProvider == nullptr)
        return crossfadeSeconds;

    auto own = trackFadeProvider(file);
    return own > 0.0 ? juce::jlimit(kMinCrossfadeSeconds, kMaxCrossfadeSeconds, own)
                      : crossfadeSeconds;
}

float PlaylistEngine::gainFor(const juce::File& file) const
{
    if (file == juce::File() || trackGainProvider == nullptr)
        return 1.0f;

    return trackGainProvider(file);
}

void PlaylistEngine::refreshTrackGains()
{
    currentTrackGain = gainFor(currentTrackFile);
    incomingTrackGain = gainFor(incomingTrackFile);

    // Applied immediately, so dragging a track's slider is audible while
    // that track is playing rather than only from its next play.
    applyDeckGains();
}

void PlaylistEngine::setShuffle(bool shouldShuffle)
{
    if (shuffleEnabled == shouldShuffle)
        return;

    shuffleEnabled = shouldShuffle;

    if (shuffleChangedCallback)
        shuffleChangedCallback(shuffleEnabled);
}

void PlaylistEngine::setShuffleChangedCallback(std::function<void(bool)> callback)
{
    shuffleChangedCallback = std::move(callback);
}

juce::File PlaylistEngine::pickNextFile()
{
    if (playOrder.isEmpty())
        return {};

    if (nextOrderIndex >= playOrder.size())
    {
        nextOrderIndex = 0;
        if (shuffleEnabled)
            for (int i = playOrder.size() - 1; i > 0; --i)
                playOrder.swap(i, random.nextInt(i + 1));
    }

    return playOrder[nextOrderIndex++];
}

void PlaylistEngine::loadIntoDeck(Deck& deck, const juce::File& file)
{
    auto* reader = formatManager.createReaderFor(file);
    if (reader == nullptr)
        return;

    deck.transport.stop();
    deck.transport.setSource(nullptr);
    deck.readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    deck.transport.setSource(deck.readerSource.get(), kReadAheadBufferSamples, &readAheadThread, reader->sampleRate);
}

void PlaylistEngine::start()
{
    // Stop everything first. This used to reset activeDeck to 0 while
    // leaving deck 1 running at whatever gain a crossfade had left it
    // at, so any future caller who used start() to change lists got two
    // decks playing over each other. crossfadeToTracks() is the
    // supported way to switch while audio is live.
    stop();
    decks[0].transport.setGain(1.0f);
    decks[1].transport.setGain(0.0f);

    auto file = pickNextFile();
    if (file == juce::File())
        return;

    activeDeck = 0;
    loadIntoDeck(decks[activeDeck], file);
    currentTrackFile = file;
    currentTrackGain = gainFor(file);
    decks[activeDeck].transport.setGain(currentTrackGain);
    decks[activeDeck].transport.start();
}

bool PlaylistEngine::isAnyDeckPlaying() const
{
    return decks[0].transport.isPlaying() || decks[1].transport.isPlaying();
}

void PlaylistEngine::seekOrderTo(const juce::File& file)
{
    nextOrderIndex = 0;
    if (file == juce::File())
        return;

    auto index = playOrder.indexOf(file);
    if (index >= 0)
        nextOrderIndex = index;
}

void PlaylistEngine::crossfadeToTracks(const juce::Array<juce::File>& tracks, const juce::File& startFrom)
{
    // Never swap in an empty list - silence mid-session is worse than
    // staying on the list that's already playing.
    if (tracks.isEmpty())
        return;

    if (!isAnyDeckPlaying())
    {
        setTracks(tracks);
        seekOrderTo(startFrom);
        start();
        return;
    }

    // Collapse any fade already in flight first, so the incoming track
    // fades in from a settled state rather than a half-faded one.
    finishCrossfadeNow();
    setTracks(tracks);
    seekOrderTo(startFrom);
    beginCrossfadeTo(pickNextFile());
}

void PlaylistEngine::crossfadeToTrackInCurrentList(const juce::File& file)
{
    if (!playOrder.contains(file))
        return;

    seekOrderTo(file);

    if (!isAnyDeckPlaying())
    {
        start();
        return;
    }

    finishCrossfadeNow();
    beginCrossfadeTo(pickNextFile());
}

void PlaylistEngine::stop()
{
    decks[0].transport.stop();
    decks[1].transport.stop();
    crossfading = false;
}

void PlaylistEngine::pause()
{
    // Collapse an in-flight fade first, so resuming doesn't come back
    // with two decks stuck at partial gain.
    finishCrossfadeNow();

    // A fade-out that was running is abandoned, not left half-applied -
    // otherwise resuming comes back quieter than it went away, with
    // nothing on screen to explain why.
    fadingOut = false;
    fadeGain = 1.0f;
    applyDeckGains();

    decks[0].transport.stop();
    decks[1].transport.stop();
}

void PlaylistEngine::resume()
{
    if (playOrder.isEmpty())
        return;

    // Pressing Play during a fade-out cancels it and comes straight back
    // up to level, rather than continuing to fade.
    if (fadingOut)
    {
        fadingOut = false;
        fadeGain = 1.0f;
        applyDeckGains();

        if (isAnyDeckPlaying())
            return;
    }

    // Nothing loaded yet (fresh session, or stopped outright) - begin at
    // the top of the order rather than doing nothing.
    if (currentTrackFile == juce::File())
    {
        start();
        return;
    }

    decks[activeDeck].transport.start();
}

void PlaylistEngine::skipToNext()
{
    // A skip during an existing crossfade used to be silently swallowed,
    // so hammering Skip (or a Stream Deck button) dropped presses. Cut
    // the fade short and start the next one instead.
    finishCrossfadeNow();
    beginCrossfade();
}

void PlaylistEngine::beginCrossfade()
{
    beginCrossfadeTo(pickNextFile());
}

void PlaylistEngine::finishCrossfadeNow()
{
    if (!crossfading)
        return;

    decks[activeDeck].transport.stop();
    decks[activeDeck].transport.setGain(1.0f); // leave it clean for reuse
    activeDeck = 1 - activeDeck;
    currentTrackFile = incomingTrackFile;
    currentTrackGain = incomingTrackGain;
    applyDeckGains(); // snap the incoming deck to its own level (under any fade-out)
    crossfading = false;
    crossfadeElapsedSeconds = 0.0;
}

void PlaylistEngine::beginCrossfadeTo(const juce::File& file)
{
    if (file == juce::File())
        return;

    int incomingDeck = 1 - activeDeck;

    if (!crossfadeEnabled)
    {
        // Straight cut. The outgoing deck is stopped rather than left to
        // run out, because this same path serves a manual Skip - where
        // leaving the old track playing to its natural end would mean it
        // carrying on underneath for minutes.
        decks[activeDeck].transport.stop();
        loadIntoDeck(decks[incomingDeck], file);

        activeDeck = incomingDeck;
        currentTrackFile = file;
        currentTrackGain = gainFor(file);
        crossfading = false;
        crossfadeElapsedSeconds = 0.0;

        applyDeckGains();
        decks[activeDeck].transport.start();
        return;
    }

    loadIntoDeck(decks[incomingDeck], file);
    decks[incomingDeck].transport.setGain(0.0f);
    decks[incomingDeck].transport.start();
    incomingTrackFile = file;
    incomingTrackGain = gainFor(file);

    crossfading = true;
    crossfadeElapsedSeconds = 0.0;

    // Captured now, from the track that is LEAVING, so the ramp keeps a
    // consistent length even if the setting changes while it runs.
    activeCrossfadeSeconds = fadeSecondsFor(currentTrackFile);

    applyDeckGains();
}

void PlaylistEngine::applyDeckGains()
{
    int incomingDeck = 1 - activeDeck;

    if (!crossfading)
    {
        decks[activeDeck].transport.setGain(currentTrackGain * fadeGain);
        return;
    }

    auto t = (float) juce::jlimit(0.0, 1.0, crossfadeElapsedSeconds / activeCrossfadeSeconds);

    // Equal-power crossfade so the perceived loudness stays roughly
    // constant through the transition instead of dipping in the middle.
    // Each deck's trim multiplies its side of the fade, so a quiet track
    // fading into a loud one keeps both trims through the transition -
    // and a fade-out in progress multiplies both.
    decks[activeDeck].transport.setGain(std::cos(t * juce::MathConstants<float>::halfPi)
                                          * currentTrackGain * fadeGain);
    decks[incomingDeck].transport.setGain(std::sin(t * juce::MathConstants<float>::halfPi)
                                            * incomingTrackGain * fadeGain);
}

double PlaylistEngine::transitionLookAheadSeconds() const
{
    // With crossfading off, the next track only needs to be started right
    // at the end. Not zero: the timer ticks every 30 ms, so a little
    // lead is what stops an audible gap opening between tracks.
    //
    // When fading, the lead is the CURRENT track's fade length - it has
    // to start handing over that far from its own end, which is exactly
    // what a per-track fade time means.
    return crossfadeEnabled ? fadeSecondsFor(currentTrackFile) : 0.05;
}

void PlaylistEngine::setCrossfadeEnabled(bool shouldCrossfade)
{
    crossfadeEnabled = shouldCrossfade;
}

void PlaylistEngine::setCrossfadeSeconds(double seconds)
{
    crossfadeSeconds = juce::jlimit(kMinCrossfadeSeconds, kMaxCrossfadeSeconds, seconds);
}

void PlaylistEngine::hardStop()
{
    stop();

    fadingOut = false;
    fadeGain = 1.0f;
    decks[0].transport.setGain(1.0f);
    decks[1].transport.setGain(1.0f);

    // Deliberately forgetting where we were - that is what makes this a
    // stop rather than a pause. resume() sees no current track and starts
    // the list from the top.
    currentTrackFile = juce::File();
    incomingTrackFile = juce::File();
    currentTrackGain = 1.0f;
    incomingTrackGain = 1.0f;
    nextOrderIndex = 0;
}

void PlaylistEngine::fadeOutAndStop(double seconds)
{
    if (!isAnyDeckPlaying())
        return;

    fadeOutSeconds = juce::jmax(0.1, seconds);
    fadeOutElapsedSeconds = 0.0;
    fadingOut = true;
}

void PlaylistEngine::timerCallback()
{
    constexpr double dt = (double) kTimerIntervalMs / 1000.0;

    if (fadingOut)
    {
        fadeOutElapsedSeconds += dt;
        fadeGain = (float) juce::jlimit(0.0, 1.0, 1.0 - fadeOutElapsedSeconds / fadeOutSeconds);
    }

    if (crossfading)
    {
        crossfadeElapsedSeconds += dt;
        applyDeckGains();

        if (crossfadeElapsedSeconds >= activeCrossfadeSeconds)
            finishCrossfadeNow();
    }
    else if (fadingOut)
    {
        applyDeckGains();
    }

    if (fadingOut)
    {
        // All the way down - stop for real rather than leaving silent
        // decks running.
        if (fadeOutElapsedSeconds >= fadeOutSeconds)
            hardStop();

        // No new transitions while fading out: starting the next track
        // underneath a fade is never what was wanted.
        return;
    }

    if (crossfading)
        return;

    auto& deck = decks[activeDeck];
    if (!deck.transport.isPlaying())
        return;

    auto remaining = deck.transport.getLengthInSeconds() - deck.transport.getCurrentPosition();
    if (remaining <= transitionLookAheadSeconds())
        beginCrossfade();
}

juce::String PlaylistEngine::getCurrentTrackName() const
{
    return currentTrackFile.getFileNameWithoutExtension();
}

void PlaylistEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    mixer.prepareToPlay(samplesPerBlockExpected, sampleRate);
    prepared = true;
    startTimer(kTimerIntervalMs);
}

void PlaylistEngine::releaseResources()
{
    stopTimer();
    mixer.releaseResources();
    prepared = false;
}

void PlaylistEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    mixer.getNextAudioBlock(bufferToFill);
}
