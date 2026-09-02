#include "SoundboardEngine.h"

namespace
{
    constexpr int kReadAheadBufferSamples = 32768;
}

SoundboardEngine::SoundboardEngine(juce::AudioFormatManager& formatManagerToUse)
    : formatManager(formatManagerToUse)
{
    // Above normal priority: this thread refills the streaming buffers
    // ahead of the real-time audio callback, and losing the CPU to
    // ordinary background work is exactly what produces dropouts.
    readAheadThread.startThread(juce::Thread::Priority::high);
}

SoundboardEngine::~SoundboardEngine()
{
    mixer.removeAllInputs();
    for (auto* voice : voices)
        voice->transport.setSource(nullptr);
    readAheadThread.stopThread(2000);
}

void SoundboardEngine::registerSound(const juce::String& name, const juce::File& file)
{
    registeredSounds[name] = file;
}

void SoundboardEngine::trigger(const juce::String& name)
{
    auto it = registeredSounds.find(name);
    if (it == registeredSounds.end())
    {
        jassertfalse; // triggered a sound that was never registered
        return;
    }

    auto* reader = formatManager.createReaderFor(it->second);
    if (reader == nullptr)
        return;

    Voice* voice = nullptr;
    for (auto* v : voices)
    {
        if (!v->transport.isPlaying())
        {
            voice = v;
            break;
        }
    }

    if (voice == nullptr && voices.size() < maxVoices)
    {
        voice = voices.add(new Voice());
        mixer.addInputSource(&voice->transport, false); // prepares it if we're already playing
    }

    // Pool exhausted and every voice is busy - steal the oldest one
    // rather than dropping the trigger silently.
    if (voice == nullptr)
        voice = voices.getFirst();

    voice->transport.stop();
    voice->transport.setSource(nullptr);
    voice->readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    voice->transport.setSource(voice->readerSource.get(), kReadAheadBufferSamples, &readAheadThread, reader->sampleRate);
    voice->transport.setGain(1.0f);
    voice->transport.start();
}

void SoundboardEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    mixer.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void SoundboardEngine::releaseResources()
{
    mixer.releaseResources();
}

void SoundboardEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    mixer.getNextAudioBlock(bufferToFill);
}
