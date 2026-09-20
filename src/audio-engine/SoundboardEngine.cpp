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

void SoundboardEngine::registerSound(const juce::String& name, const juce::File& file, float linearGain,
                                     bool looping)
{
    registeredSounds[name] = { file, linearGain, looping };
}

bool SoundboardEngine::hasSound(const juce::String& name) const
{
    return registeredSounds.find(name) != registeredSounds.end();
}

juce::File SoundboardEngine::getSoundFile(const juce::String& name) const
{
    auto it = registeredSounds.find(name);
    return it == registeredSounds.end() ? juce::File() : it->second.file;
}

juce::StringArray SoundboardEngine::getRegisteredNames() const
{
    juce::StringArray names;
    for (const auto& pair : registeredSounds)
        names.add(pair.first);
    return names;
}

void SoundboardEngine::removeSound(const juce::String& name)
{
    registeredSounds.erase(name);
}

void SoundboardEngine::clearSounds()
{
    // Voices keep playing whatever they already loaded - each owns its
    // own reader, created per trigger, independent of this map.
    registeredSounds.clear();
}

void SoundboardEngine::stopAllVoices()
{
    for (auto* voice : voices)
        voice->transport.stop();
}

void SoundboardEngine::stop(const juce::String& name)
{
    for (auto* voice : voices)
        if (voice->name == name)
            voice->transport.stop();
}

bool SoundboardEngine::isPlaying(const juce::String& name) const
{
    for (auto* voice : voices)
        if (voice->name == name && voice->transport.isPlaying())
            return true;

    return false;
}

juce::StringArray SoundboardEngine::getPlayingLoopNames() const
{
    juce::StringArray names;

    for (auto* voice : voices)
        if (voice->looping && voice->transport.isPlaying())
            names.addIfNotAlreadyThere(voice->name);

    return names;
}

void SoundboardEngine::trigger(const juce::String& name)
{
    auto it = registeredSounds.find(name);
    if (it == registeredSounds.end())
        return; // unknown name is ordinary user error - see the header

    // A loop that is already running: the same trigger stops it. Checked
    // before a reader is created, so stopping costs nothing.
    if (it->second.looping && isPlaying(name))
    {
        stop(name);
        return;
    }

    auto* reader = formatManager.createReaderFor(it->second.file);
    if (reader == nullptr)
        return;

    auto gain = it->second.gain;

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

    // Pool exhausted and every voice is busy - steal one rather than
    // dropping the trigger silently, but never a running LOOP while a
    // one-shot is available: pulling the rain out from under a battle to
    // play a door creak is worse than losing the creak.
    if (voice == nullptr)
    {
        for (auto* v : voices)
        {
            if (! v->looping)
            {
                voice = v;
                break;
            }
        }
    }

    if (voice == nullptr)
        voice = voices.getFirst();

    voice->transport.stop();
    voice->transport.setSource(nullptr);
    voice->name = name;
    voice->looping = it->second.looping;
    voice->readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    voice->readerSource->setLooping(it->second.looping);
    voice->transport.setSource(voice->readerSource.get(), kReadAheadBufferSamples, &readAheadThread, reader->sampleRate);
    voice->transport.setGain(gain);
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
