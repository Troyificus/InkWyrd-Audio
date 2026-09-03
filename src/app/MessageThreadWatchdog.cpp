#include "MessageThreadWatchdog.h"

#include "Log.h"

namespace
{
    constexpr int kTickIntervalMs = 100;
}

MessageThreadWatchdog::MessageThreadWatchdog(int stallThresholdMs)
    : thresholdMs(stallThresholdMs)
{
    lastTickMs.store(juce::Time::currentTimeMillis());
    startTimer(kTickIntervalMs);

    checker = std::thread([this]
    {
        while (!shouldStop.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kTickIntervalMs));

            auto since = juce::Time::currentTimeMillis() - lastTickMs.load();
            if (since < thresholdMs)
                continue;

            // Wait for it to actually end, then report the REAL length.
            // The stamp we saw is the last one written BEFORE the block;
            // the next one is written the moment the thread runs again,
            // so the difference between them is the stall.
            auto stalledAt = lastTickMs.load();

            while (!shouldStop.load() && lastTickMs.load() == stalledAt)
                std::this_thread::sleep_for(std::chrono::milliseconds(kTickIntervalMs));

            if (shouldStop.load())
                return;

            logLine("[Watchdog] UI thread was blocked for about "
                     + juce::String(lastTickMs.load() - stalledAt)
                     + " ms - the app would have been unresponsive for that long.");
        }
    });
}

MessageThreadWatchdog::~MessageThreadWatchdog()
{
    stopTimer();
    shouldStop.store(true);

    if (checker.joinable())
        checker.join();
}

void MessageThreadWatchdog::timerCallback()
{
    lastTickMs.store(juce::Time::currentTimeMillis());
}
