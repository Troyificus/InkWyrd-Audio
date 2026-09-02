#include "PlaylistEngine.h"

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
    playOrder.clear();
    nextOrderIndex = 0;

    // Recursive: people point this at an album or library folder whose
    // audio lives a level or two down. Real beta report - the chosen
    // folder held only cover art at the top level and 26 tracks in
    // per-track subfolders, so nothing played and nothing said why.
    for (const auto& entry : juce::RangedDirectoryIterator(folder, true, "*", juce::File::findFiles))
    {
        auto file = entry.getFile();
        if (formatManager.findFormatForFileExtension(file.getFileExtension()) != nullptr)
            playOrder.add(file);
    }

    if (shuffleEnabled)
        for (int i = playOrder.size() - 1; i > 0; --i)
            playOrder.swap(i, random.nextInt(i + 1));
}

void PlaylistEngine::setShuffle(bool shouldShuffle)
{
    shuffleEnabled = shouldShuffle;
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
    auto file = pickNextFile();
    if (file == juce::File())
        return;

    activeDeck = 0;
    loadIntoDeck(decks[activeDeck], file);
    decks[activeDeck].transport.setGain(1.0f);
    decks[activeDeck].transport.start();
    currentTrackFile = file;
}

void PlaylistEngine::stop()
{
    decks[0].transport.stop();
    decks[1].transport.stop();
    crossfading = false;
}

void PlaylistEngine::skipToNext()
{
    if (!crossfading)
        beginCrossfade();
}

void PlaylistEngine::beginCrossfade()
{
    auto file = pickNextFile();
    if (file == juce::File())
        return;

    int incomingDeck = 1 - activeDeck;
    loadIntoDeck(decks[incomingDeck], file);
    decks[incomingDeck].transport.setGain(0.0f);
    decks[incomingDeck].transport.start();
    incomingTrackFile = file;

    crossfading = true;
    crossfadeElapsedSeconds = 0.0;
    applyCrossfadeGains();
}

void PlaylistEngine::applyCrossfadeGains()
{
    auto t = (float) juce::jlimit(0.0, 1.0, crossfadeElapsedSeconds / crossfadeDurationSeconds);
    int incomingDeck = 1 - activeDeck;

    // Equal-power crossfade so the perceived loudness stays roughly
    // constant through the transition instead of dipping in the middle.
    decks[activeDeck].transport.setGain(std::cos(t * juce::MathConstants<float>::halfPi));
    decks[incomingDeck].transport.setGain(std::sin(t * juce::MathConstants<float>::halfPi));
}

void PlaylistEngine::timerCallback()
{
    if (crossfading)
    {
        crossfadeElapsedSeconds += (double) kTimerIntervalMs / 1000.0;
        applyCrossfadeGains();

        if (crossfadeElapsedSeconds >= crossfadeDurationSeconds)
        {
            decks[activeDeck].transport.stop();
            activeDeck = 1 - activeDeck;
            currentTrackFile = incomingTrackFile;
            crossfading = false;
        }
        return;
    }

    auto& deck = decks[activeDeck];
    if (!deck.transport.isPlaying())
        return;

    auto remaining = deck.transport.getLengthInSeconds() - deck.transport.getCurrentPosition();
    if (remaining <= crossfadeDurationSeconds)
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
