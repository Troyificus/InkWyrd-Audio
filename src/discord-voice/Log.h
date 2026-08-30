#pragma once

#include <iostream>
#include <mutex>
#include <juce_core/juce_core.h>

// juce::Logger::writeToLog() with no logger installed falls back to
// OutputDebugString on Windows - invisible in a plain console window.
// This spike is meant to be run and watched from a terminal, so it
// needs real stdout output instead.
//
// Gateway and voice-gateway callbacks fire on ixwebsocket's own
// background threads while the main thread logs too - without a lock,
// two lines can interleave mid-write (seen in practice: one line landed
// with no trailing newline before the next began).
inline void logLine(const juce::String& message)
{
    static std::mutex logMutex;
    const std::lock_guard<std::mutex> lock(logMutex);
    std::cout << message << std::endl;
}
