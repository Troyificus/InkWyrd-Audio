#pragma once

#include <map>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

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
    void registerSound(const juce::String& name, const juce::File& file, float linearGain = 1.0f,
                        bool looping = false);

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

        bool isFading() const { return ! juce::approximatelyEqual(fadeLevel, fadeTarget); }
    };

    // Finds a free voice (or steals one - see the .cpp) and starts `name`
    // on it at the given fade level. nullptr if the file can't be read.
    Voice* startVoice(const juce::String& name, float initialFadeLevel);

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
    };

    std::map<juce::String, RegisteredSound> registeredSounds;
    juce::OwnedArray<Voice> voices;
    juce::MixerAudioSource mixer;

    static constexpr int maxVoices = 16;
};
