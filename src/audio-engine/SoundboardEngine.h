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
    void trigger(const juce::String& name);

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
