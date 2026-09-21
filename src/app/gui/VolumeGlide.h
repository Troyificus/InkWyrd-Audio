#pragma once

#include <functional>

#include <juce_events/juce_events.h>

// Moves the master volume to a scene's level over a few seconds rather
// than jumping. The master fader is what Discord hears too, so a scene
// that snapped it from 40% to 90% would be exactly the kind of surprise
// the table notices.
//
// Message thread only, like everything else that drives the fader.
class VolumeGlide : private juce::Timer
{
public:
    ~VolumeGlide() override { stopTimer(); }

    // Called on every step with the level to apply, and once more when
    // the glide arrives - the second is where the new level is saved, so
    // settings aren't rewritten thirty times a second.
    std::function<void(float)> apply;
    std::function<void(float)> arrived;

    void start(float from, float to, double seconds)
    {
        startLevel = juce::jlimit(0.0f, 1.0f, from);
        targetLevel = juce::jlimit(0.0f, 1.0f, to);
        durationSeconds = juce::jmax(0.05, seconds);
        startedAt = juce::Time::getMillisecondCounterHiRes();

        if (juce::approximatelyEqual(startLevel, targetLevel))
        {
            finish();
            return;
        }

        startTimerHz(30);
    }

    // The user took hold of the fader: they win, immediately, and the
    // glide doesn't drag it back on its next step.
    void cancel() { stopTimer(); }

    bool isGliding() const { return isTimerRunning(); }

private:
    void timerCallback() override
    {
        auto t = (juce::Time::getMillisecondCounterHiRes() - startedAt) / (durationSeconds * 1000.0);
        if (t >= 1.0)
        {
            finish();
            return;
        }

        if (apply != nullptr)
            apply(startLevel + (targetLevel - startLevel) * (float) t);
    }

    void finish()
    {
        stopTimer();

        if (apply != nullptr)
            apply(targetLevel);

        if (arrived != nullptr)
            arrived(targetLevel);
    }

    float startLevel = 1.0f, targetLevel = 1.0f;
    double durationSeconds = 1.0;
    double startedAt = 0.0;
};
