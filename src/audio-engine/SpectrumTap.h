#pragma once

#include <atomic>
#include <array>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

// Feeds the Player window's spectrum display.
//
// The split of work is the whole design: the AUDIO thread only ever
// copies samples into a circular buffer and bumps an index - no FFT, no
// allocation, no locks. The UI thread does the transform, at whatever
// rate it repaints. A dropped or duplicated frame is invisible in a
// visualiser, so nothing here needs to be exact; it needs to be cheap
// and never block the audio thread.
//
// Deliberately NOT a lock-free FIFO with claim/commit semantics. The
// reader wants "the most recent N samples", not "every sample exactly
// once" - a FIFO would add back-pressure and bookkeeping for a consumer
// that is happy to miss things.
class SpectrumTap
{
public:
    // 2^11 = 2048 samples at 48kHz is ~43ms of audio, which is enough
    // resolution at the bottom end to separate bass bands without the
    // display smearing over transients.
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int numBands = 48;

    // AUDIO THREAD. Takes a mono sum of whatever it's given.
    void pushBlock(const juce::AudioBuffer<float>& buffer, int numSamples);

    // UI THREAD. Fills `bandsOut` with 0..1 magnitudes, low to high, and
    // applies its own decay so the caller doesn't have to keep state.
    // Returns false when nothing has been pushed yet, so a caller can
    // draw a flat idle display rather than noise.
    bool readBands(std::array<float, numBands>& bandsOut);

private:
    // Written by the audio thread, read by the UI thread. Individual
    // float reads/writes race by design - the worst case is one sample
    // of one frame being torn, which cannot be seen.
    std::array<float, fftSize> ring {};
    std::atomic<int> writeIndex { 0 };
    std::atomic<bool> hasAudio { false };

    // UI thread only.
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize * 2> scratch {};
    std::array<float, numBands> smoothed {};
};
