#include "NoiseSuppressor.h"

#include <cmath>
#include <cstring>

#include <rnnoise.h>

namespace
{
    // RNNoise works in int16 RANGE expressed as floats (-32768..32767),
    // not the -1..1 JUCE uses everywhere else. This is the single
    // easiest thing to get wrong about the library: feed it normalised
    // audio and it does almost nothing, because every sample looks like
    // silence to it. Upstream's own rnnoise_demo.c confirms the
    // convention - it reads shorts straight into floats with no scaling.
    constexpr float kRnnoiseScale = 32768.0f;

    // Append to a compacting buffer. Returns false if it wouldn't fit,
    // in which case nothing is written.
    bool append(juce::AudioBuffer<float>& buffer, int& stored, const float* source, int numSamples)
    {
        if (stored + numSamples > buffer.getNumSamples())
            return false;

        juce::FloatVectorOperations::copy(buffer.getWritePointer(0) + stored, source, numSamples);
        stored += numSamples;
        return true;
    }

    // Drop the first numSamples and slide the rest down.
    void consume(juce::AudioBuffer<float>& buffer, int& stored, int numSamples)
    {
        jassert(numSamples <= stored);
        auto remaining = stored - numSamples;
        if (remaining > 0)
            std::memmove(buffer.getWritePointer(0),
                          buffer.getReadPointer(0) + numSamples,
                          sizeof(float) * (size_t) remaining);
        stored = remaining;
    }
}

NoiseSuppressor::NoiseSuppressor() = default;

NoiseSuppressor::~NoiseSuppressor()
{
    if (state != nullptr)
        rnnoise_destroy(state);
}

void NoiseSuppressor::prepare(double sampleRate, int maximumBlockSize)
{
    deviceSampleRate = sampleRate > 0.0 ? sampleRate : (double) kRnnoiseSampleRate;
    needsResampling = std::abs(deviceSampleRate - (double) kRnnoiseSampleRate) > 1.0;

    // A block at the device rate becomes at most this many 48k samples.
    // If a device ever exceeded the scratch capacity we would rather
    // stay off than allocate on the audio thread or write out of bounds.
    auto worstCase = (int) std::ceil(maximumBlockSize * ((double) kRnnoiseSampleRate / deviceSampleRate))
                      + kRnnoiseFrameSamples * (kOutputPrimeFrames + 2);
    prepared = worstCase <= kScratchCapacity && maximumBlockSize <= kScratchCapacity;
    jassert(prepared);

    if (state == nullptr)
        state = rnnoise_create(nullptr);

    reset();
}

void NoiseSuppressor::reset()
{
    toRnnoiseRate.reset();
    fromRnnoiseRate.reset();

    pendingInput.clear();
    pendingOutput.clear();
    pendingInputSamples = 0;
    underruns.store(0);

    // Prime the output with silence so the first blocks have something
    // to read while the pipeline fills. This IS the added latency - see
    // kOutputPrimeFrames in the header.
    pendingOutputSamples = kOutputPrimeFrames * kRnnoiseFrameSamples;
}

int NoiseSuppressor::getLatencySamples() const
{
    if (! enabled.load() || ! prepared)
        return 0;

    // RNNoise's own algorithmic delay, measured at 960 samples (20ms) in
    // the mic-spike harness by cross-correlating its output against the
    // clean reference - not taken from documentation.
    constexpr int kRnnoiseInternalDelay = 960;

    auto at48k = kRnnoiseInternalDelay + kOutputPrimeFrames * kRnnoiseFrameSamples;
    return (int) std::round(at48k * (deviceSampleRate / (double) kRnnoiseSampleRate));
}

double NoiseSuppressor::getLatencyMs() const
{
    // Deliberately independent of `enabled` and `prepared`: the UI needs
    // to state the cost of switching it ON, and a figure that reads 0
    // until you commit is useless for that. It is also rate-independent
    // - the delay is a fixed amount of TIME, whatever the device rate.
    constexpr int kRnnoiseInternalDelay = 960;
    return 1000.0 * (kRnnoiseInternalDelay + kOutputPrimeFrames * kRnnoiseFrameSamples)
            / (double) kRnnoiseSampleRate;
}

void NoiseSuppressor::processAvailableFrames()
{
    while (pendingInputSamples >= kRnnoiseFrameSamples
            && pendingOutputSamples + kRnnoiseFrameSamples <= pendingOutput.getNumSamples())
    {
        auto* frame = pendingInput.getWritePointer(0);

        juce::FloatVectorOperations::multiply(frame, kRnnoiseScale, kRnnoiseFrameSamples);
        rnnoise_process_frame(state, frame, frame); // in-place is supported
        juce::FloatVectorOperations::multiply(frame, 1.0f / kRnnoiseScale, kRnnoiseFrameSamples);

        append(pendingOutput, pendingOutputSamples, frame, kRnnoiseFrameSamples);
        consume(pendingInput, pendingInputSamples, kRnnoiseFrameSamples);
    }
}

void NoiseSuppressor::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (! enabled.load() || ! prepared || state == nullptr || numSamples <= 0)
        return;

    auto numChannels = buffer.getNumChannels();
    if (numChannels < 1 || numSamples > buffer.getNumSamples())
        return;

    // 1. Collapse to mono. Channel 0 alone rather than an average: for a
    // duplicated mono mic they are identical, and for a genuine stereo
    // pair averaging can partially cancel if the two capsules are out of
    // phase - a worse failure than ignoring one side.
    auto* mono = monoScratch.getWritePointer(0);
    juce::FloatVectorOperations::copy(mono, buffer.getReadPointer(0), numSamples);

    // 2. Up to 48k if the device isn't already there. speedRatio for
    // JUCE's interpolators is inputRate/outputRate.
    const auto upRatio = deviceSampleRate / (double) kRnnoiseSampleRate;
    int samplesAt48k = numSamples;
    const float* toEnqueue = mono;

    if (needsResampling)
    {
        samplesAt48k = juce::jmin((int) std::floor(numSamples / upRatio), kScratchCapacity);
        if (samplesAt48k <= 0)
            return;
        toRnnoiseRate.process(upRatio, mono, resampledScratch.getWritePointer(0), samplesAt48k);
        toEnqueue = resampledScratch.getReadPointer(0);
    }

    if (! append(pendingInput, pendingInputSamples, toEnqueue, samplesAt48k))
    {
        // Only reachable if the output side stalled and back-pressured
        // the input. Start clean rather than blocking the audio thread.
        pendingInputSamples = 0;
        return;
    }

    // 3. Whole frames only - RNNoise has no partial-frame mode.
    processAvailableFrames();

    // 4. Back down to the device rate, producing exactly numSamples.
    if (needsResampling)
    {
        const auto downRatio = (double) kRnnoiseSampleRate / deviceSampleRate;

        // +4 covers the interpolator's own lookahead within a call.
        auto needed = (int) std::ceil(numSamples * downRatio) + 4;
        if (pendingOutputSamples < needed)
        {
            underruns.fetch_add(1);
            buffer.clear(0, numSamples);
            return;
        }

        auto used = fromRnnoiseRate.process(downRatio, pendingOutput.getReadPointer(0),
                                             mono, numSamples);
        consume(pendingOutput, pendingOutputSamples, juce::jmin(used, pendingOutputSamples));
    }
    else
    {
        if (pendingOutputSamples < numSamples)
        {
            underruns.fetch_add(1);
            buffer.clear(0, numSamples);
            return;
        }

        juce::FloatVectorOperations::copy(mono, pendingOutput.getReadPointer(0), numSamples);
        consume(pendingOutput, pendingOutputSamples, numSamples);
    }

    // 5. The same suppressed signal to every channel - see the header on
    // why this is deliberately mono.
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::copy(buffer.getWritePointer(ch), mono, numSamples);
}
