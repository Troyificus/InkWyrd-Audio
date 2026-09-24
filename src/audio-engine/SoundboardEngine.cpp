#include "SoundboardEngine.h"

namespace
{
    constexpr int kReadAheadBufferSamples = 32768;

    // How often a fade moves. At 30 Hz a step is ~33 ms, and the
    // transport ramps smoothly across the block between steps, so this
    // is inaudible as steps.
    constexpr int kFadeTickHz = 30;

    // Random play only has to be as accurate as "somewhere in the next
    // couple of minutes", so a quarter-second tick is plenty.
    constexpr int kRandomTickHz = 4;
}

juce::String randomFrequencyName(RandomFrequency frequency)
{
    switch (frequency)
    {
        case RandomFrequency::low:    return "low";
        case RandomFrequency::medium: return "medium";
        case RandomFrequency::high:   return "high";
        case RandomFrequency::off:    break;
    }
    return "off";
}

RandomFrequency randomFrequencyFromName(const juce::String& name)
{
    if (name == "low")    return RandomFrequency::low;
    if (name == "medium") return RandomFrequency::medium;
    if (name == "high")   return RandomFrequency::high;
    return RandomFrequency::off;
}

SoundboardEngine::SoundboardEngine(juce::AudioFormatManager& formatManagerToUse)
    : formatManager(formatManagerToUse)
{
    // Above normal priority: this thread refills the streaming buffers
    // ahead of the real-time audio callback, and losing the CPU to
    // ordinary background work is exactly what produces dropouts.
    readAheadThread.startThread(juce::Thread::Priority::high);

    randomTimer.tick = [this] { runRandomScheduler(); };
}

SoundboardEngine::~SoundboardEngine()
{
    stopTimer();
    randomTimer.stopTimer();
    mixer.removeAllInputs();
    for (auto* voice : voices)
        voice->transport.setSource(nullptr);
    readAheadThread.stopThread(2000);
}

void SoundboardEngine::registerSound(const juce::String& name, const juce::File& file, float linearGain,
                                     bool looping, SoundFades fades)
{
    registeredSounds[name] = { file, linearGain, looping, fades };
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
    // Instant, deliberately, fades and all: this is the panic button. It
    // switches random play off too - silencing everything and then having
    // a seagull go off ten seconds later isn't silence.
    stopAllRandom();

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

int SoundboardEngine::countPlaying(const juce::String& name) const
{
    auto count = 0;
    for (auto* voice : voices)
        if (voice->name == name && voice->transport.isPlaying())
            ++count;
    return count;
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
    // The same length both ways: a scene from before per-sound fades
    // faded its loops in and out over the one scene transition.
    startLoop(name, SoundFades { fadeSeconds, fadeSeconds });
}

void SoundboardEngine::startLoop(const juce::String& name, SoundFades fades)
{
    auto it = registeredSounds.find(name);
    if (it == registeredSounds.end() || ! it->second.looping)
        return;

    // Already running, or on its way out: bring THAT voice back up rather
    // than starting a second copy - the file carries on from where it is.
    // It takes on the new fades, so the scene now in charge decides how
    // it leaves.
    for (auto* voice : voices)
    {
        if (voice->name == name && voice->transport.isPlaying())
        {
            voice->fades = fades;
            voice->startedByScene = true;
            beginFade(*voice, 1.0f, fades.fadeInSeconds, false);
            return;
        }
    }

    if (auto* voice = startWithFades(name, fades))
        voice->startedByScene = true;
}

void SoundboardEngine::stopLoopForScene(const juce::String& name, double transitionSeconds)
{
    for (auto* voice : voices)
    {
        if (voice->name != name || ! voice->looping || ! voice->transport.isPlaying())
            continue;

        auto seconds = voice->startedByScene || voice->fades.fadeOutSeconds > 0.0
                           ? voice->fades.fadeOutSeconds
                           : transitionSeconds;
        beginFade(*voice, 0.0f, seconds, true);
    }
}

bool SoundboardEngine::wasStartedByScene(const juce::String& name) const
{
    for (auto* voice : voices)
        if (voice->name == name && voice->transport.isPlaying() && ! voice->stopWhenSilent)
            return voice->startedByScene;
    return false;
}

void SoundboardEngine::stopLoopWithItsOwnFade(const juce::String& name)
{
    for (auto* voice : voices)
        if (voice->name == name && voice->looping && voice->transport.isPlaying())
            beginFade(*voice, 0.0f, voice->fades.fadeOutSeconds, true);
}

SoundFades SoundboardEngine::getFades(const juce::String& name) const
{
    for (auto* voice : voices)
        if (voice->name == name && voice->transport.isPlaying() && ! voice->stopWhenSilent)
            return voice->fades;

    auto it = registeredSounds.find(name);
    return it == registeredSounds.end() ? SoundFades {} : it->second.fades;
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
        ensureFadeTimerRunning();
    }
    else if (stopAtEnd && voice.fadeLevel <= 0.0f)
    {
        voice.transport.stop();
        voice.stopWhenSilent = false;
        voice.fadeLevel = voice.fadeTarget = 1.0f;
    }
}

void SoundboardEngine::ensureFadeTimerRunning()
{
    if (! isTimerRunning())
        startTimerHz(kFadeTickHz);
}

bool SoundboardEngine::voiceNeedsTimer(const Voice& voice) const
{
    // Fading now, or a one-shot that will fade itself out as it nears its
    // end and has to be watched until it gets there.
    return voice.isFading()
            || (! voice.looping && voice.transport.isPlaying()
                && voice.fades.fadeOutSeconds > 0.0 && ! voice.endFadeStarted);
}

void SoundboardEngine::timerCallback()
{
    auto anyStillFading = false;

    // A one-shot close enough to its own end starts fading out, so its
    // tail dies away rather than cutting.
    for (auto* voice : voices)
    {
        if (voice->looping || voice->endFadeStarted || voice->fades.fadeOutSeconds <= 0.0
             || ! voice->transport.isPlaying())
            continue;

        auto remaining = voice->transport.getLengthInSeconds() - voice->transport.getCurrentPosition();
        if (remaining <= voice->fades.fadeOutSeconds)
        {
            voice->endFadeStarted = true;
            beginFade(*voice, 0.0f, juce::jmax(0.01, remaining), true);
        }
    }

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

    // Kept running while any voice still needs watching, not just while
    // one is fading.
    for (auto* voice : voices)
        anyStillFading = anyStillFading || voiceNeedsTimer(*voice);

    if (! anyStillFading)
        stopTimer();
}

void SoundboardEngine::trigger(const juce::String& name)
{
    auto it = registeredSounds.find(name);
    if (it == registeredSounds.end())
        return; // unknown name is ordinary user error - see the header

    // A loop that is already running: the same trigger stops it - fading
    // out if it's set to. Checked before a reader is created, so stopping
    // costs nothing.
    if (it->second.looping && isPlaying(name))
    {
        stopLoopWithItsOwnFade(name);
        return;
    }

    startWithFades(name, it->second.fades);
}

SoundboardEngine::Voice* SoundboardEngine::startWithFades(const juce::String& name, SoundFades fades)
{
    auto fadesIn = fades.fadeInSeconds > 0.0;
    auto* voice = startVoice(name, fadesIn ? 0.0f : 1.0f);
    if (voice == nullptr)
        return nullptr;

    voice->fades = fades;

    if (fadesIn)
        beginFade(*voice, 1.0f, fades.fadeInSeconds, false);

    if (voiceNeedsTimer(*voice))
        ensureFadeTimerRunning();

    return voice;
}

//==============================================================================
double SoundboardEngine::nowSeconds() const
{
    return clockForTesting != nullptr ? clockForTesting()
                                      : juce::Time::getMillisecondCounterHiRes() / 1000.0;
}

void SoundboardEngine::setClockForTesting(std::function<double()> secondsNow)
{
    clockForTesting = std::move(secondsNow);
}

double SoundboardEngine::pickGap(RandomFrequency frequency)
{
    auto range = randomRangeFor(frequency);
    return range.minSeconds + random.nextDouble() * (range.maxSeconds - range.minSeconds);
}

void SoundboardEngine::startRandom(const juce::String& name, RandomFrequency frequency)
{
    auto it = registeredSounds.find(name);
    startRandom(name, frequency, it == registeredSounds.end() ? SoundFades {} : it->second.fades);
}

void SoundboardEngine::startRandom(const juce::String& name, RandomFrequency frequency, SoundFades fades)
{
    auto it = registeredSounds.find(name);
    if (frequency == RandomFrequency::off || it == registeredSounds.end() || it->second.looping)
    {
        stopRandom(name);
        return;
    }

    auto existing = randomSounds.find(name);
    if (existing != randomSounds.end() && existing->second.frequency == frequency)
    {
        // Same pace: keep the schedule it already has, so re-applying a
        // scene doesn't keep pushing the next seagull further away.
        existing->second.fades = fades;
        return;
    }

    // The FIRST play comes sooner than a normal gap - somewhere in the
    // first quarter-to-whole of the shortest gap - so switching it on is
    // answered within a reasonable wait, not up to five minutes later.
    auto range = randomRangeFor(frequency);
    auto first = range.minSeconds * (0.25 + 0.75 * random.nextDouble());

    randomSounds[name] = { frequency, fades, nowSeconds() + first };

    if (! randomTimer.isTimerRunning())
        randomTimer.startTimerHz(kRandomTickHz);
}

void SoundboardEngine::stopRandom(const juce::String& name)
{
    // Stops FUTURE plays only: one already playing finishes normally.
    randomSounds.erase(name);
    if (randomSounds.empty())
        randomTimer.stopTimer();
}

void SoundboardEngine::stopAllRandom()
{
    randomSounds.clear();
    randomTimer.stopTimer();
}

RandomFrequency SoundboardEngine::getRandomFrequency(const juce::String& name) const
{
    auto it = randomSounds.find(name);
    return it == randomSounds.end() ? RandomFrequency::off : it->second.frequency;
}

std::vector<SoundboardEngine::RandomSound> SoundboardEngine::getRandomSounds() const
{
    std::vector<RandomSound> result;
    for (const auto& [name, state] : randomSounds)
        result.push_back({ name, state.frequency, state.fades });
    return result;
}

double SoundboardEngine::getNextRandomTime(const juce::String& name) const
{
    auto it = randomSounds.find(name);
    return it == randomSounds.end() ? 0.0 : it->second.nextTime;
}

void SoundboardEngine::runRandomScheduler()
{
    auto now = nowSeconds();

    for (auto it = randomSounds.begin(); it != randomSounds.end();)
    {
        auto registered = registeredSounds.find(it->first);

        // Its button was cleared, or made a loop, since random was
        // switched on: nothing sensible to fire any more.
        if (registered == registeredSounds.end() || registered->second.looping)
        {
            it = randomSounds.erase(it);
            continue;
        }

        auto& state = it->second;
        if (now >= state.nextTime)
        {
            // Never on top of itself: if the last one is still going,
            // this turn is simply skipped.
            if (! isPlaying(it->first))
                startWithFades(it->first, state.fades);

            state.nextTime = now + pickGap(state.frequency);
        }

        ++it;
    }

    if (randomSounds.empty())
        randomTimer.stopTimer();
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
    voice->fades = {};
    voice->endFadeStarted = false;
    voice->startedByScene = false;

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
