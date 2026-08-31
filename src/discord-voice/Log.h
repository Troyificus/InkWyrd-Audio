#pragma once

#include <iostream>
#include <mutex>
#include <juce_core/juce_core.h>

// juce::Logger::writeToLog() with no logger installed falls back to
// OutputDebugString on Windows - invisible in a plain console window.
// This spike is meant to be run and watched from a terminal, so it
// needs real stdout output instead.
//
// Also writes to a real file: InkwyrdAudioApp is a GUI (Windows-
// subsystem) binary with no attached console at all, so std::cout from
// it is discarded silently - the file is the only place its Discord
// connection logs (including everything GatewayClient/VoiceGatewayClient/
// DaveSession log) are visible at all. The console spike/test apps still
// get the stdout copy too, unaffected.
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

    auto logFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("Inkwyrd Audio")
                       .getChildFile("log.txt");
    logFile.getParentDirectory().createDirectory();

    // Truncated once per process run (not per call) so a fresh launch
    // starts a fresh log, but every line within that run accumulates.
    static bool truncatedThisRun = false;
    if (!truncatedThisRun)
    {
        logFile.deleteFile();
        truncatedThisRun = true;
    }
    logFile.appendText(message + "\n");
}
