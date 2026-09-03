#pragma once

#include <atomic>
#include <thread>

#include <juce_events/juce_events.h>

// Logs any stall of the message (UI) thread longer than a threshold.
//
// Exists because a real "the app froze and I couldn't click anything,
// but other programs were fine" report could not be reproduced, and
// reading the code produced several plausible culprits and no proof.
// Rather than guess-fix one of them, this makes the next occurrence
// identify itself: the log gets a line saying how long the message
// thread was blocked and when.
//
// Deliberately cheap: a juce::Timer that stamps a clock on the message
// thread, and one background thread that notices when that stamp stops
// advancing. Nothing is allocated or locked on either side.
//
// NOTE a stall recorded here means the message thread was genuinely
// busy. If the app appears frozen and NOTHING is logged, the cause is
// elsewhere - the likeliest being a modal dialog opened off-screen (see
// gui/Dialogs.h), which swallows input without blocking anything.
class MessageThreadWatchdog : private juce::Timer
{
public:
    explicit MessageThreadWatchdog(int stallThresholdMs = 300);
    ~MessageThreadWatchdog() override;

private:
    void timerCallback() override;

    std::atomic<juce::int64> lastTickMs { 0 };
    std::atomic<bool> shouldStop { false };
    const int thresholdMs;
    std::thread checker;
};
