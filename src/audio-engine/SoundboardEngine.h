#pragma once

#include <functional>
#include <map>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

// How a sound fades in when it starts and out when it stops. 0 = off (a
// straight start or cut). Per button, and a scene keeps its own copy for
// the sounds it runs - see Scene::soundFades.
struct SoundFades
{
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;

    static constexpr double kMaxSeconds = 10.0;

    bool operator==(const SoundFades& other) const
    {
        return juce::approximatelyEqual(fadeInSeconds, other.fadeInSeconds)
                && juce::approximatelyEqual(fadeOutSeconds, other.fadeOutSeconds);
    }
    bool operator!=(const SoundFades& other) const { return ! (*this == other); }
};

// How often a sound set to play randomly fires. Each gap is a fresh random
// pick within its range, so it never settles into a pattern you can hear.
enum class RandomFrequency { off = 0, low, medium, high };

struct RandomRange { double minSeconds, maxSeconds; };

inline RandomRange randomRangeFor(RandomFrequency frequency)
{
    switch (frequency)
    {
        case RandomFrequency::low:    return { 120.0, 300.0 }; // every 2-5 min
        case RandomFrequency::medium: return { 45.0, 120.0 };  // every 45 s - 2 min
        case RandomFrequency::high:   return { 15.0, 45.0 };   // every 15-45 s
        case RandomFrequency::off:    break;
    }
    return { 0.0, 0.0 };
}

juce::String randomFrequencyName(RandomFrequency frequency);   // "low", "medium", ...
RandomFrequency randomFrequencyFromName(const juce::String& name); // off if unknown

// A pool of one-shot players, triggerable at any time independent of
// playlist state. Registered sounds can be triggered repeatedly and
// overlap with themselves (e.g. a rapid-fire sword-clash sound effect) -
// each trigger() call gets its own voice from the pool.
class SoundboardEngine : public juce::AudioSource,
                          private juce::Timer
{
public:
    explicit SoundboardEngine(juce::AudioFormatManager& formatManagerToUse);
    ~SoundboardEngine() override;

    // linearGain is the button's own volume trim. Applied when the sound
    // is triggered, so changing it takes effect from the next press
    // (these are one-shots - there is nothing sensible to do to a clip
    // that is already halfway through playing).
    // looping makes this sound a LATCH rather than a one-shot: it
    // repeats until something stops it, and triggering it again is what
    // stops it. That is what an ambience bed needs - rain under a
    // battle - which a one-shot pool can't express.
    //
    // fades: how it fades in on EVERY start (a click, a Stream Deck key, a
    // random trigger) and out when it stops - a loop being stopped, or a
    // one-shot reaching its own end, whose last fadeOutSeconds are faded.
    // The Killswitch (stopAllVoices) ignores them: it's a panic button.
    void registerSound(const juce::String& name, const juce::File& file, float linearGain = 1.0f,
                        bool looping = false, SoundFades fades = {});

    // A name that isn't registered is a no-op, not an error - Stream Deck
    // buttons carry free-text names typed by the user, so a mismatch is
    // ordinary user error rather than a programming mistake.
    //
    // For a LOOPING sound this toggles: triggering one that is already
    // running stops it. One button, one key and one Stream Deck press
    // therefore start and stop an ambience bed, with nothing new to
    // learn on any of the three.
    void trigger(const juce::String& name);

    // Stops this sound wherever it is playing. A one-shot is normally
    // left to finish; this is for a loop, which otherwise never would.
    void stop(const juce::String& name);

    // Whether any voice is currently playing this sound. Used by the
    // board to show a running loop; called from the message thread, the
    // same thread that starts and stops voices.
    bool isPlaying(const juce::String& name) const;

    // How many voices are playing this sound - more than one for a
    // one-shot pressed again before it finished.
    int countPlaying(const juce::String& name) const;

    // Names of every looping sound running right now, so the UI can ask
    // once rather than per button. A loop that is fading OUT is not
    // listed: it is on its way out, and a scene that wants it has to
    // bring it back.
    juce::StringArray getPlayingLoopNames() const;

    // Scene changes. Unlike trigger(), these are not toggles: they say
    // what should be TRUE afterwards, so pressing a scene twice is
    // harmless. And they fade, because a scene change where the music
    // crossfades smoothly but the rain cuts dead sounds broken.
    //
    // startLoop on a loop that is already running (or fading out) brings
    // it back up to full without restarting the file - which is what
    // lets rain carry on uninterrupted from one scene into the next.
    // Both ignore anything that isn't a registered LOOPING sound; a
    // scene never fires one-shots.
    void startLoop(const juce::String& name, double fadeSeconds);
    void stopLoop(const juce::String& name, double fadeSeconds);

    // A scene's own fades for a loop, overriding the button's: fades in
    // over fades.fadeInSeconds, and remembers fades.fadeOutSeconds for
    // whenever it's stopped later - by the next scene, or by a click.
    void startLoop(const juce::String& name, SoundFades fades);

    // Stops a loop with the fade-out it was STARTED with (by its button
    // or by a scene), so a sound always leaves the way it was set up to.
    void stopLoopWithItsOwnFade(const juce::String& name);

    // A scene stopping a loop. One a scene started leaves exactly as that
    // scene said, a deliberate cut included. One started from its button
    // uses the button's fade-out if it has one, otherwise
    // `transitionSeconds` - so ambience still eases out on a scene change
    // when nobody has set anything, as it always did.
    void stopLoopForScene(const juce::String& name, double transitionSeconds);

    // Whether a scene (rather than a click) started this running sound.
    bool wasStartedByScene(const juce::String& name) const;

    // The fades a running sound was started with, or the button's own if
    // it isn't running. What saving a scene records.
    SoundFades getFades(const juce::String& name) const;

    //==========================================================================
    // Random play: a one-shot that fires by itself every so often - the
    // seagull over the waves. Clicking the button still plays it at once;
    // this runs alongside. Never overlaps itself: if the previous one is
    // still playing when its time comes, that turn is skipped. Loops can't
    // be random (they never finish); asking is ignored.
    //
    // fades: what each random play uses (a scene's own, or the button's).
    void startRandom(const juce::String& name, RandomFrequency frequency);
    void startRandom(const juce::String& name, RandomFrequency frequency, SoundFades fades);
    void stopRandom(const juce::String& name);
    void stopAllRandom();

    RandomFrequency getRandomFrequency(const juce::String& name) const; // off if not random

    struct RandomSound
    {
        juce::String name;
        RandomFrequency frequency;
        SoundFades fades;
    };
    std::vector<RandomSound> getRandomSounds() const;

    // Test hooks: a clock in seconds and a seed, so the self-test can move
    // time on instead of waiting minutes, and runRandomScheduler() to do
    // what the timer does.
    void setClockForTesting(std::function<double()> secondsNow);
    void setRandomSeedForTesting(juce::int64 seed) { random.setSeed(seed); }
    void runRandomScheduler();

    // When a random sound will next fire, in the clock's seconds (0 if it
    // isn't random). For the self-test.
    double getNextRandomTime(const juce::String& name) const;

    // 0..1: where this sound's fade has got to (1 when not fading, or
    // not playing at all). For the self-test, which has no other way to
    // see a fade happen without an audio device.
    float getFadeLevel(const juce::String& name) const;

    bool hasSound(const juce::String& name) const;
    juce::File getSoundFile(const juce::String& name) const; // {} if not registered
    juce::StringArray getRegisteredNames() const;            // alphabetical (std::map order)

    void removeSound(const juce::String& name);

    // Safe to call while sounds are playing: each voice owns its own
    // reader, independent of this map.
    void clearSounds();

    // The Killswitch: every voice, instantly, and random play off too.
    void stopAllVoices();

    // juce::AudioSource
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

private:
    struct Voice
    {
        std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
        juce::AudioTransportSource transport;

        // What this voice is playing, so a loop can be found again and
        // stopped. Empty once it has been used for nothing yet.
        juce::String name;
        bool looping = false;

        // The button's own trim, and where a fade has got to on top of
        // it. Kept apart so a fade never loses the trim.
        float baseGain = 1.0f;
        float fadeLevel = 1.0f;
        float fadeTarget = 1.0f;
        float fadeStepPerTick = 0.0f;
        bool stopWhenSilent = false;

        // What this voice was started with. fadeOutSeconds is used when a
        // loop is stopped, and for a one-shot's own ending: when it gets
        // within that many seconds of its end it fades out by itself.
        SoundFades fades;
        bool endFadeStarted = false;
        bool startedByScene = false;

        bool isFading() const { return ! juce::approximatelyEqual(fadeLevel, fadeTarget); }
    };

    // Finds a free voice (or steals one - see the .cpp) and starts `name`
    // on it at the given fade level. nullptr if the file can't be read.
    Voice* startVoice(const juce::String& name, float initialFadeLevel);

    // Starts `name` with its fades: silent and rising if it fades in.
    Voice* startWithFades(const juce::String& name, SoundFades fades);

    void ensureFadeTimerRunning();
    bool voiceNeedsTimer(const Voice& voice) const;

    void beginFade(Voice& voice, float target, double seconds, bool stopAtEnd);

    // Message thread, and only while something is fading: this is what
    // moves each fading voice a step, the same way PlaylistEngine's own
    // timer drives its crossfades.
    void timerCallback() override;

    juce::AudioFormatManager& formatManager;
    juce::TimeSliceThread readAheadThread { "SoundboardEngine read-ahead" };
    struct RegisteredSound
    {
        juce::File file;
        float gain = 1.0f;
        bool looping = false;
        SoundFades fades;
    };

    struct RandomState
    {
        RandomFrequency frequency = RandomFrequency::off;
        SoundFades fades;
        double nextTime = 0.0;
    };

    double nowSeconds() const;
    double pickGap(RandomFrequency frequency);

    // Its own timer, separate from the fade one: random play ticks along
    // for as long as anything is random, fades only while fading.
    struct RandomTimer : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { if (tick) tick(); }
    };

    std::map<juce::String, RandomState> randomSounds;
    RandomTimer randomTimer;
    juce::Random random;
    std::function<double()> clockForTesting;

    std::map<juce::String, RegisteredSound> registeredSounds;
    juce::OwnedArray<Voice> voices;
    juce::MixerAudioSource mixer;

    static constexpr int maxVoices = 16;
};
