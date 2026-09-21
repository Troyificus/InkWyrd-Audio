#include "SoundboardEngine.h"

namespace
{
    constexpr int kReadAheadBufferSamples = 32768;

    // How often a fade moves. At 30 Hz a step is ~33 ms, and the
    // transport ramps smoothly across the block between steps, so this
    // is inaudible as steps.
    constexpr int kFadeTickHz = 30;
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
    stopTimer();
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
    // Instant, deliberately, fades and all: this is the panic button.
    for (auto* voice : voices)
    {
        voice->transport.stop();
        voice->fadeLevel = voice->fadeTarget = 1.0f;
        voice->stopWhenSilent = false;
    }
}

void SoundboardEngine::stop(const juce::String& name)
{
    for (auto* voice : voices)
    {
        if (voice->name == name)
        {
            voice->transport.stop();
            voice->fadeLevel = voice->fadeTarget = 1.0f;
            voice->stopWhenSilent = false;
        }
    }
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
        if (voice->looping && voice->transport.isPlaying() && ! voice->stopWhenSilent)
            names.addIfNotAlreadyThere(voice->name);

    return names;
}

void SoundboardEngine::startLoop(const juce::String& name, double fadeSeconds)
{
    auto it = registeredSounds.find(name);
    if (it == registeredSounds.end() || ! it->second.looping)
        return;

    // Already running, or on its way out: bring THAT voice back up rather
    // than starting a second copy - the file carries on from where it is.
    for (auto* voice : voices)
    {
        if (voice->name == name && voice->transport.isPlaying())
        {
            beginFade(*voice, 1.0f, fadeSeconds, false);
            return;
        }
    }

    if (auto* voice = startVoice(name, 0.0f))
        beginFade(*voice, 1.0f, fadeSeconds, false);
}

void SoundboardEngine::stopLoop(const juce::String& name, double fadeSeconds)
{
    for (auto* voice : voices)
        if (voice->name == name && voice->looping && voice->transport.isPlaying())
            beginFade(*voice, 0.0f, fadeSeconds, true);
}

float SoundboardEngine::getFadeLevel(const juce::String& name) const
{
    for (auto* voice : voices)
        if (voice->name == name && voice->transport.isPlaying())
            return voice->fadeLevel;

    return 1.0f;
}

void SoundboardEngine::beginFade(Voice& voice, float target, double seconds, bool stopAtEnd)
{
    voice.fadeTarget = target;
    voice.stopWhenSilent = stopAtEnd;

    auto ticks = juce::jmax(1.0, seconds * kFadeTickHz);
    voice.fadeStepPerTick = (float) (std::abs(target - voice.fadeLevel) / ticks);

    // A zero-length fade is just a jump - settle it now rather than
    // waiting a tick.
    if (seconds <= 0.0 || voice.fadeStepPerTick <= 0.0f)
        voice.fadeLevel = target;

    voice.transport.setGain(voice.baseGain * voice.fadeLevel);

    if (voice.isFading())
    {
        if (! isTimerRunning())
            startTimerHz(kFadeTickHz);
    }
    else if (stopAtEnd && voice.fadeLevel <= 0.0f)
    {
        voice.transport.stop();
        voice.stopWhenSilent = false;
        voice.fadeLevel = voice.fadeTarget = 1.0f;
    }
}

void SoundboardEngine::timerCallback()
{
    auto anyStillFading = false;

    for (auto* voice : voices)
    {
        if (! voice->isFading())
            continue;

        auto rising = voice->fadeTarget > voice->fadeLevel;
        voice->fadeLevel += rising ? voice->fadeStepPerTick : -voice->fadeStepPerTick;

        // Clamp onto the target rather than stepping past it.
        if (rising ? voice->fadeLevel >= voice->fadeTarget : voice->fadeLevel <= voice->fadeTarget)
            voice->fadeLevel = voice->fadeTarget;

        voice->transport.setGain(voice->baseGain * voice->fadeLevel);

        if (voice->isFading())
        {
            anyStillFading = true;
        }
        else if (voice->stopWhenSilent && voice->fadeLevel <= 0.0f)
        {
            // All the way down: actually stop, so the voice is free again
            // and the board stops showing it as running.
            voice->transport.stop();
            voice->stopWhenSilent = false;
            voice->fadeLevel = voice->fadeTarget = 1.0f;
        }
    }

    if (! anyStillFading)
        stopTimer();
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

    startVoice(name, 1.0f);
}

SoundboardEngine::Voice* SoundboardEngine::startVoice(const juce::String& name, float initialFadeLevel)
{
    auto it = registeredSounds.find(name);
    if (it == registeredSounds.end())
        return nullptr;

    auto* reader = formatManager.createReaderFor(it->second.file);
    if (reader == nullptr)
        return nullptr;

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

    voice->baseGain = gain;
    voice->fadeLevel = voice->fadeTarget = initialFadeLevel;
    voice->fadeStepPerTick = 0.0f;
    voice->stopWhenSilent = false;

    voice->transport.setGain(gain * initialFadeLevel);
    voice->transport.start();
    return voice;
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
