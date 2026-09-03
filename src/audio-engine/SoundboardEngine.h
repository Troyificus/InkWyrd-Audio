#pragma once

#include <map>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

// A pool of one-shot players, triggerable at any time independent of
// playlist state. Registered sounds can be triggered repeatedly and
// overlap with themselves (e.g. a rapid-fire sword-clash sound effect) -
// each trigger() call gets its own voice from the pool.
class SoundboardEngine : public juce::AudioSource
{
public:
    explicit SoundboardEngine(juce::AudioFormatManager& formatManagerToUse);
    ~SoundboardEngine() override;

    void registerSound(const juce::String& name, const juce::File& file);

    // A name that isn't registered is a no-op, not an error - Stream Deck
    // buttons carry free-text names typed by the user, so a mismatch is
    // ordinary user error rather than a programming mistake.
    void trigger(const juce::String& name);

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
    };

    juce::AudioFormatManager& formatManager;
    juce::TimeSliceThread readAheadThread { "SoundboardEngine read-ahead" };
    std::map<juce::String, juce::File> registeredSounds;
    juce::OwnedArray<Voice> voices;
    juce::MixerAudioSource mixer;

    static constexpr int maxVoices = 16;
};
