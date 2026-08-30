#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <juce_audio_basics/juce_audio_basics.h>

#include "VoiceGatewayClient.h"
#include "VoiceUdpSocket.h"

struct OpusEncoder;

// Bridges the real-time audio callback (device-rate blocks, arbitrary
// size) to Discord's fixed cadence (20ms / 960-sample frames at 48kHz)
// via a lock-free FIFO and a background thread that does the actual
// Opus encode + DAVE encrypt + RTP send - none of which belongs on the
// audio thread (encoding is real-time-safe-ish, but the network send
// and DAVE crypto calls are not guaranteed to be, and don't need to be
// on that thread anyway).
//
// Stereo throughout: this is a mixed music+voice signal, not pure
// speech, so unlike the discord-spike's mono test tone, channel
// separation from the playlist is worth keeping. ssrc/secret key are
// already baked into voiceGateway/udp by the time this runs - see
// Main.cpp's connection sequence.
class DiscordAudioSender
{
public:
    DiscordAudioSender(VoiceGatewayClient& voiceGatewayToUse, VoiceUdpSocket& udpToUse);
    ~DiscordAudioSender();

    void start();
    void stop();

    // Real-time safe: called from the audio callback with whatever
    // sample rate the device is actually running at. Internally
    // resampled to 48kHz before being queued. This resampling is
    // "good enough for voice chat" (small per-block drift tolerated,
    // self-corrects via the FIFO), not sample-accurate mastering-grade.
    void pushSamples(const juce::AudioBuffer<float>& buffer, double sourceSampleRate);

private:
    void senderThreadLoop();

    VoiceGatewayClient& voiceGateway;
    VoiceUdpSocket& udp;

    static constexpr int kDiscordSampleRate = 48000;
    static constexpr int kFrameSamples = 960; // 20ms at 48kHz
    static constexpr int kResampleScratchCapacity = 8192; // generous vs. any real device block size

    juce::LagrangeInterpolator resamplers[2];
    // Reused across calls (setSize with avoidReallocating) so
    // pushSamples never allocates on the audio thread.
    juce::AudioBuffer<float> resampleScratch { 2, kResampleScratchCapacity };

    juce::AbstractFifo fifo { kDiscordSampleRate * 2 }; // ~2s headroom
    juce::AudioBuffer<float> fifoBuffer { 2, kDiscordSampleRate * 2 };

    std::atomic<bool> running { false };
    std::unique_ptr<std::thread> senderThread;

    OpusEncoder* opusEncoder = nullptr;
};
