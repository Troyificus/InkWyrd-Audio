#include "SpectrumTap.h"

#include <cmath>

namespace
{
    // Where each display band starts, as a fraction of the spectrum,
    // spaced logarithmically. Linear spacing would give forty bands of
    // treble nobody can hear moving and three of bass doing all the
    // work - music lives in the bottom two octaves of this range.
    constexpr float kLowestBinHz = 40.0f;
    constexpr float kHighestBinHz = 16000.0f;

    // How fast a bar falls once the sound behind it stops. Rise is
    // instant: a meter that lags the transient looks broken, while one
    // that lingers on the way down looks like a meter.
    constexpr float kDecayPerFrame = 0.12f;
}

void SpectrumTap::pushBlock(const juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto channels = buffer.getNumChannels();
    if (channels <= 0 || numSamples <= 0)
        return;

    auto index = writeIndex.load(std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            sum += buffer.getReadPointer(ch)[i];

        ring[(size_t) index] = sum / (float) channels;
        index = (index + 1) & (fftSize - 1);
    }

    writeIndex.store(index, std::memory_order_release);
    hasAudio.store(true, std::memory_order_relaxed);
}

bool SpectrumTap::readBands(std::array<float, numBands>& bandsOut)
{
    if (! hasAudio.load(std::memory_order_relaxed))
        return false;

    // Copy oldest-to-newest ending at the write cursor, so the window
    // sees a contiguous stretch of time rather than a wrap.
    auto end = writeIndex.load(std::memory_order_acquire);
    for (int i = 0; i < fftSize; ++i)
        scratch[(size_t) i] = ring[(size_t) ((end + i) & (fftSize - 1))];

    std::fill(scratch.begin() + fftSize, scratch.end(), 0.0f);

    window.multiplyWithWindowingTable(scratch.data(), fftSize);
    fft.performFrequencyOnlyForwardTransform(scratch.data());

    // Only the first half of the transform is meaningful, and the bin
    // width is the Nyquist rate divided by that half.
    constexpr float kAssumedSampleRate = 48000.0f;
    const auto binHz = (kAssumedSampleRate * 0.5f) / (float) (fftSize / 2);

    for (int band = 0; band < numBands; ++band)
    {
        auto lowProportion = (float) band / (float) numBands;
        auto highProportion = (float) (band + 1) / (float) numBands;

        auto lowHz = kLowestBinHz * std::pow(kHighestBinHz / kLowestBinHz, lowProportion);
        auto highHz = kLowestBinHz * std::pow(kHighestBinHz / kLowestBinHz, highProportion);

        auto firstBin = juce::jlimit(1, fftSize / 2 - 1, (int) (lowHz / binHz));
        auto lastBin = juce::jlimit(firstBin, fftSize / 2 - 1, (int) (highHz / binHz));

        // Peak rather than average across the band: an average washes
        // out a narrow tone sitting inside a wide high band, which is
        // exactly the thing worth seeing.
        float peak = 0.0f;
        for (int bin = firstBin; bin <= lastBin; ++bin)
            peak = juce::jmax(peak, scratch[(size_t) bin]);

        // To dB, then mapped onto a 60dB window. Linear magnitude puts
        // everything quiet in the bottom pixel and only moves for the
        // loudest peaks.
        auto db = juce::Decibels::gainToDecibels(peak / (float) (fftSize / 4), -100.0f);
        auto level = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);

        auto& previous = smoothed[(size_t) band];
        previous = level > previous ? level : juce::jmax(0.0f, previous - kDecayPerFrame);
        bandsOut[(size_t) band] = previous;
    }

    return true;
}
