#pragma once

#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "DiscordAudioSender.h"
#include "PlaylistEngine.h"
#include "SoundboardEngine.h"
#include "PluginChain.h"

// The one real-time audio callback for the whole app. Combines what
// AudioSourcePlayer and AudioProcessorPlayer each handle separately -
// mic input running through the VST3 chain (processor-style) summed
// with the playlist+soundboard mix (source-style) - because both need
// to land in the same output buffer and the same outgoing Discord
// stream, and JUCE doesn't have a single wrapper for that combination.
class MasterEngine : public juce::AudioIODeviceCallback
{
public:
    MasterEngine(PlaylistEngine& playlistToUse, SoundboardEngine& soundboardToUse, PluginChain& voiceChainToUse);

    // nullptr disables Discord streaming (local monitoring only). Safe
    // to call from any thread - this pointer is only ever read on the
    // audio thread, never dereferenced for mutation here.
    void setDiscordSender(DiscordAudioSender* sender);

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    PlaylistEngine& playlist;
    SoundboardEngine& soundboard;
    PluginChain& voiceChain;

    juce::MixerAudioSource musicMixer; // playlist + soundboard

    std::atomic<DiscordAudioSender*> discordSender { nullptr };

    juce::AudioBuffer<float> micBuffer, masterBuffer;
    juce::MidiBuffer scratchMidi;

    double currentSampleRate = 44100.0;
};
