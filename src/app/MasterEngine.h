#pragma once

#include <atomic>
#include <functional>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "DiscordAudioSender.h"
#include "NoiseSuppressor.h"
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

    // Silences the mic before it reaches the VST chain - affects both
    // local monitoring and whatever's sent to Discord. Safe to call
    // from any thread (Stream Deck control commands land on the
    // message thread; this is read on the audio thread).
    void setMicMuted(bool muted);
    bool isMicMuted() const { return micMuted.load(); }

    // Fires only when the mic mute state actually CHANGES, on whatever
    // thread changed it - the Player window's button and the Stream Deck
    // control socket are both real callers, so a listener hung off one
    // of those UIs would miss the other. Assign once during startup,
    // before anything can toggle the mic; it is deliberately not
    // guarded for concurrent reassignment.
    std::function<void(bool micMutedNow)> onMicMuteChanged;

    // Whether the master mix is also played out of the host's own
    // speakers. Off while streaming to Discord: the host is in the call
    // too, so they'd otherwise hear everything twice - once locally and
    // again (slightly later) via the bot's stream, which sounds like a
    // delay/echo. Left on when there's no Discord connection, so the
    // app is still usable/testable standalone. Safe from any thread.
    void setLocalMonitoring(bool shouldMonitor) { localMonitoring.store(shouldMonitor); }
    bool isLocalMonitoring() const { return localMonitoring.load(); }

    // The master fader: 0 = silence, 1 = unity. Applied to the finished
    // mix, so it affects BOTH local monitoring and what Discord receives
    // - that is what makes it the master rather than a monitor trim.
    // Safe from any thread; the audio thread ramps towards it rather
    // than jumping, so dragging the slider doesn't produce zipper noise.
    void setMasterGain(float gain) { masterGain.store(juce::jlimit(0.0f, 1.0f, gain)); }
    float getMasterGain() const { return masterGain.load(); }

    // RNNoise on the mic, ahead of the VST chain. Off by default and
    // owned here rather than by the caller so the audio thread always
    // has a valid object whatever the UI is doing. See NoiseSuppressor.h
    // for why this is a toggle and not always-on.
    NoiseSuppressor& getNoiseSuppressor() { return noiseSuppressor; }

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
    NoiseSuppressor noiseSuppressor;

    std::atomic<DiscordAudioSender*> discordSender { nullptr };
    std::atomic<bool> micMuted { false };
    std::atomic<bool> localMonitoring { true };
    std::atomic<float> masterGain { 1.0f };

    // Audio thread only - where the gain ramp got to last block.
    float lastMasterGain = 1.0f;

    juce::AudioBuffer<float> micBuffer, masterBuffer;
    juce::MidiBuffer scratchMidi;

    double currentSampleRate = 44100.0;
};
