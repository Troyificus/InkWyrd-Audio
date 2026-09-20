#pragma once

#include <atomic>
#include <cmath>

#include <juce_core/juce_core.h>

namespace inkwyrd
{
    // How far the music drops, and what counts as speech. Defaults are
    // the ones that sound right narrating over a bed: enough of a drop
    // to be clearly heard over, not so much that the music disappears
    // and the room notices it coming back.
    struct DuckSettings
    {
        bool enabled = false;

        // How far down the music goes while the mic is live.
        float amountDb = -12.0f;

        // The mic level that counts as speaking. Below this, nothing
        // ducks - which is what stops a noisy room from holding the
        // music down all session.
        float thresholdDb = -40.0f;

        // Fixed rather than exposed: these are the numbers nobody tunes
        // well by ear, and getting them wrong is what makes ducking
        // sound like a fault. Attack is fast enough not to clip the
        // first word; the hold is what stops the music surging back up
        // between words.
        static constexpr float attackMs = 30.0f;
        static constexpr float holdMs = 400.0f;
        static constexpr float releaseMs = 600.0f;
    };

    // Works out, block by block, what gain the music should be at.
    //
    // Deliberately separate from MasterEngine so it can be driven by the
    // self-test with made-up mic levels: the whole behaviour worth
    // checking (drops on speech, STAYS down through a pause, comes back
    // afterwards, never moves at all when switched off) is in here, and
    // none of it needs an audio device.
    //
    // Settings are atomics because the message thread writes them while
    // the audio thread reads them.
    class DuckEnvelope
    {
    public:
        void prepare(double sampleRateToUse)
        {
            sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 44100.0;
            currentGain = 1.0f;
            holdSamplesLeft = 0;
        }

        void setSettings(const DuckSettings& s)
        {
            enabled.store(s.enabled);
            amountDb.store(s.amountDb);
            thresholdDb.store(s.thresholdDb);
        }

        bool isEnabled() const { return enabled.load(); }

        // The gain the music should reach by the END of this block, given
        // the mic's peak level over it. Audio thread only.
        //
        // Block-rate rather than per-sample: a block is a few
        // milliseconds against an attack of thirty, and the caller ramps
        // between the last value and this one, so nothing steps.
        float processBlock(float micPeakLinear, int numSamples)
        {
            if (! enabled.load())
            {
                // Still glides back rather than snapping, so switching
                // ducking off mid-sentence isn't a jump in level.
                holdSamplesLeft = 0;
                currentGain = moveTowards(currentGain, 1.0f, DuckSettings::releaseMs, numSamples);
                return currentGain;
            }

            auto speaking = micPeakLinear > juce::Decibels::decibelsToGain(thresholdDb.load(), -100.0f);

            if (speaking)
                holdSamplesLeft = (int) (DuckSettings::holdMs * 0.001 * sampleRate);
            else
                holdSamplesLeft = juce::jmax(0, holdSamplesLeft - numSamples);

            auto duckedGain = juce::Decibels::decibelsToGain(amountDb.load(), -100.0f);
            auto holding = speaking || holdSamplesLeft > 0;
            auto target = holding ? duckedGain : 1.0f;

            currentGain = moveTowards(currentGain, target,
                                       holding ? DuckSettings::attackMs : DuckSettings::releaseMs,
                                       numSamples);
            return currentGain;
        }

        float getCurrentGain() const { return currentGain; }

    private:
        // One exponential step of `numSamples` towards the target. A time
        // constant, not a straight line: the fast part of the move
        // happens first, which is what makes a duck sound like a hand on
        // a fader rather than a switch.
        float moveTowards(float from, float to, float timeMs, int numSamples) const
        {
            if (juce::approximatelyEqual(from, to))
                return to;

            auto blockSeconds = (double) numSamples / sampleRate;
            auto tau = juce::jmax(0.001, (double) timeMs * 0.001);
            auto step = 1.0 - std::exp(-blockSeconds / tau);

            auto moved = from + (float) ((to - from) * step);

            // Close enough is the target: otherwise the gain creeps
            // towards 1.0 forever and never actually gets there.
            return std::abs(to - moved) < 0.0005f ? to : moved;
        }

        std::atomic<bool> enabled { false };
        std::atomic<float> amountDb { -12.0f };
        std::atomic<float> thresholdDb { -40.0f };

        double sampleRate = 44100.0;
        float currentGain = 1.0f;
        int holdSamplesLeft = 0;
    };
}
