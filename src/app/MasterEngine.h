#pragma once

#include <atomic>
#include <functional>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "DiscordAudioSender.h"
#include "NoiseSuppressor.h"
#include "SpectrumTap.h"
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
    MasterEngine(PlaylistEngine& playlistToUse, SoundboardEngine& soundboardToUse,
                  PluginChain& voiceChainToUse, juce::AudioFormatManager& formatManagerToUse);

    //==============================================================================
    // PREVIEW: auditioning a track from the Library.
    //
    // Deliberately NOT part of the master mix. It is summed into the
    // local output only, after the Discord send, so the room never hears
    // what you are auditioning and the spectrum display keeps showing
    // what Discord actually gets.
    //
    // It also ignores the local-monitoring switch, which is off by
    // default while Discord is connected - a preview that obeyed it
    // would be silent exactly when someone most wants to audition
    // something.
    //
    // Message thread only. Returns false if the file couldn't be read.
    bool startPreview(const juce::File& file, float linearGain);
    void stopPreview();

    bool isPreviewing() const { return previewActive.load(); }
    juce::File getPreviewFile() const { return previewFile; }

    // Broadcasts when the preview starts or stops, INCLUDING when it
    // reaches the end of the file on its own (juce_AudioTransportSource
    // sends a change message when the stream finishes), which is how the
    // app knows to resume a playlist it paused.
    juce::ChangeBroadcaster& getPreviewBroadcaster() { return previewTransport; }
    bool isPreviewTransportPlaying() const { return previewTransport.isPlaying(); }

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

    // The finished master mix, tapped for the Player window's spectrum
    // display. Taken AFTER the master fader, so what the display shows
    // is what Discord is actually being sent - a meter that ignores the
    // fader would say the same thing whether the room could hear
    // anything or not.
    SpectrumTap& getSpectrumTap() { return spectrumTap; }

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
    juce::AudioFormatManager& formatManager;

    // The preview player. Its own transport rather than a deck of
    // PlaylistEngine's: previewing must not disturb the playlist's
    // position, its crossfade, or what it thinks is playing.
    juce::AudioTransportSource previewTransport;
    std::unique_ptr<juce::AudioFormatReaderSource> previewReader;
    juce::AudioBuffer<float> previewBuffer;
    juce::File previewFile;                  // message thread only
    std::atomic<bool> previewActive { false };

    juce::MixerAudioSource musicMixer; // playlist + soundboard
    NoiseSuppressor noiseSuppressor;
    SpectrumTap spectrumTap;

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
