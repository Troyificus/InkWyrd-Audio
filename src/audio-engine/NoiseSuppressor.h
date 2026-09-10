#pragma once

#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

struct DenoiseState;

// RNNoise on the mic path, wrapped so the real-time callback can hand it
// arbitrary block sizes at whatever rate the audio device happens to be
// running.
//
// Three impedance mismatches to absorb, all of them real:
//
//  * RNNoise is 48kHz ONLY. There is no other rate; the model was
//    trained at 48k and the band layout is hard-coded to it. WASAPI
//    devices are usually already there, but 44.1k is common enough that
//    silently doing nothing would be a bad answer. So when the device
//    rate differs, audio is resampled to 48k, processed, and resampled
//    back - same juce::LagrangeInterpolator approach DiscordAudioSender
//    already uses for its own 48k conversion.
//  * RNNoise works in fixed 480-sample frames (10ms). Device blocks are
//    whatever the driver chose - 441, 480, 512, 1024. Bridged with FIFOs
//    on both sides rather than by assuming any relationship between the
//    two.
//  * RNNoise is MONO. The mic path here is two channels, but for a
//    microphone those are either the same signal duplicated or a stereo
//    pair of one voice. Either way the right thing is to suppress one
//    mono signal and write it to both, not to run two independent
//    suppressors that could gate differently and smear the image.
//
// THE COST, stated up front because it is not free: this adds latency to
// the voice path. RNNoise itself delays by 20ms, plus one 10ms frame of
// input buffering, plus a small priming cushion - see kOutputPrimeFrames.
// getLatencySamples() reports the real figure.
//
// It also does real harm to an already-clean mic. Measured (see
// CLAUDE.md): on a noisy input (5dB SNR) it improves speech-band SNR by
// +5.1dB, but on a quiet one (20dB SNR) it costs -9.2dB. That is why
// this defaults to OFF and is a user toggle rather than always-on.
class NoiseSuppressor
{
public:
    NoiseSuppressor();
    ~NoiseSuppressor();

    // Safe to call from any thread; read on the audio thread. Turning it
    // off is immediate and clean - process() becomes a no-op, so there
    // is no tail of half-suppressed audio.
    void setEnabled(bool shouldBeEnabled) { enabled.store(shouldBeEnabled); }
    bool isEnabled() const { return enabled.load(); }

    // Audio thread, and must not allocate. Call from
    // audioDeviceAboutToStart before any process() call.
    void prepare(double sampleRate, int maximumBlockSize);
    void reset();

    // Suppresses in place. Expects the mono-or-duplicated mic buffer;
    // channel 0 is what gets analysed, and the result is written to
    // every channel. A no-op when disabled or unprepared.
    void process(juce::AudioBuffer<float>& buffer, int numSamples);

    // Added delay on the voice path, in samples at the CURRENT device
    // rate, or 0 when disabled. Exposed rather than hidden because it is
    // a real cost the user is choosing to pay.
    int getLatencySamples() const;

    // The same figure in milliseconds, which is what a UI wants - the
    // sample count is at the device rate, so a caller converting it
    // needs to know that rate too. Reports the steady-state figure even
    // when currently disabled, so the toggle can say what turning it on
    // will cost.
    double getLatencyMs() const;

    // How many times the output FIFO has run dry since prepare(). Should
    // stay at 0 in steady state; a climbing number means the priming
    // cushion is too small for this device's block pattern. Diagnostic
    // only - underruns emit silence rather than glitching.
    int getUnderrunCount() const { return underruns.load(); }

    static constexpr int kRnnoiseSampleRate = 48000;
    static constexpr int kRnnoiseFrameSamples = 480;

private:
    void processAvailableFrames();

    // How much processed audio to hold back before the first block is
    // allowed to consume any. Without a cushion, a device block that
    // asks for slightly more 48k samples than the resampler just
    // produced (a routine occurrence when the rates aren't related by a
    // neat ratio) underruns on the very first call and keeps doing it.
    // Two frames is 20ms - enough to absorb that, small enough not to
    // dominate the delay RNNoise already imposes.
    static constexpr int kOutputPrimeFrames = 2;

    // Sized for any plausible device block at any plausible rate, with
    // room for the resample ratio; prepare() asserts against it rather
    // than reallocating on the audio thread.
    static constexpr int kScratchCapacity = 16384;

    DenoiseState* state = nullptr;
    std::atomic<bool> enabled { false };
    std::atomic<int> underruns { 0 };

    double deviceSampleRate = 48000.0;
    bool needsResampling = false;
    bool prepared = false;

    juce::LagrangeInterpolator toRnnoiseRate, fromRnnoiseRate;

    // Everything below is audio-thread-only and preallocated.
    juce::AudioBuffer<float> monoScratch { 1, kScratchCapacity };
    juce::AudioBuffer<float> resampledScratch { 1, kScratchCapacity };

    // Plain compacting buffers rather than juce::AbstractFifo, and the
    // reason is specific: juce::LagrangeInterpolator::process() decides
    // for itself how many INPUT samples it needs to produce a requested
    // number of outputs, and reports that back. A FIFO can't un-read the
    // surplus, so anything read-but-unused would have to be thrown away
    // - a few samples per block, every block, which is a slow drift and
    // a click every time it accumulates past a sample. These consume
    // exactly what the interpolator reports using. The memmove that
    // costs is over a few hundred floats and allocates nothing.
    juce::AudioBuffer<float> pendingInput { 1, kScratchCapacity };
    juce::AudioBuffer<float> pendingOutput { 1, kScratchCapacity };
    int pendingInputSamples = 0;
    int pendingOutputSamples = 0;
};
