#pragma once

#include <atomic>
#include <iostream>
#include <mutex>
#include <juce_core/juce_core.h>

#include "LogRedaction.h"

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
//
// Every line goes through inkwyrd::redactSecrets first - see
// LogRedaction.h for why that net is there now the log is something
// people are asked to attach to a bug report.

// Where the logs live. A folder rather than two loose files, so
// "Open log folder" in Settings has somewhere to point and a tester has
// one obvious thing to attach.
inline juce::File inkwyrdLogFolder()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("Inkwyrd Audio")
               .getChildFile("logs");
}

inline juce::File inkwyrdLogFile()
{
    return inkwyrdLogFolder().getChildFile("log.txt");
}

// How many finished runs to keep alongside the current one.
inline constexpr int kLogRunsKept = 20;

inline juce::File inkwyrdPreviousLogFile()
{
    return inkwyrdLogFolder().getChildFile("log-previous.txt");
}

// Rotates once per process run: the last run's log is KEPT as
// log-previous.txt rather than deleted.
//
// This is the difference between a crash report that can be acted on and
// one that can't. The first thing anyone does after a crash is open the
// program again - which, when the log was simply truncated at startup,
// destroyed the only record of what had just happened before they ever
// got as far as reporting it.
// Keeps the newest `keep` archived runs in `folder` and deletes the rest.
//
// Separated out and given its own checks because it DELETES FILES, and
// the files it deletes are the only record of what the app was doing
// when something went wrong. A pattern that was slightly too greedy
// would quietly destroy exactly what it exists to preserve.
//
// Only matches the timestamped archive (log-2*.txt). log.txt itself,
// log-previous.txt from before this scheme, and log-before-upgrade.txt
// are all left alone.
inline void inkwyrdPruneLogArchive(const juce::File& folder, int keep)
{
    juce::Array<juce::File> archived;
    folder.findChildFiles(archived, juce::File::findFiles, false, "log-2*.txt");

    if (archived.size() <= keep)
        return;

    // Every name begins with its timestamp, so sorting by name sorts by
    // age, oldest first.
    struct ByName
    {
        static int compareElements(const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compare(b.getFileName());
        }
    };
    ByName sorter;
    archived.sort(sorter);

    for (int i = 0; i < archived.size() - keep; ++i)
        archived[i].deleteFile();
}

inline void inkwyrdRotateLogsOnce()
{
    // Atomic, not a plain bool: logLine() calls this under its mutex but
    // logCrashLine() deliberately does not, so two threads really can
    // reach it at once and a non-atomic flag would be a data race.
    static std::atomic<bool> rotated { false };
    if (rotated.exchange(true))
        return;

    auto folder = inkwyrdLogFolder();
    folder.createDirectory();

    // Archive the run that just ended, named for when it ended so the
    // files sort chronologically.
    //
    // This used to keep exactly ONE previous run, which turned out to be
    // the wrong number by a long way. A three and a half hour stream
    // crashed four times, and by the time anyone looked, every crash log
    // had been overwritten by the restarts that followed it - the
    // evidence was destroyed by the act of carrying on. That is the
    // normal way a bad session goes, not an unusual one.
    auto current = inkwyrdLogFile();
    if (current.existsAsFile())
    {
        auto stamp = current.getLastModificationTime().formatted("%Y-%m-%d_%H%M%S");
        current.moveFileTo(folder.getChildFile("log-" + stamp + ".txt"));
    }

    // The single kept-previous file from before this change, given a real
    // timestamp so it joins the archive rather than sitting outside it.
    auto previous = inkwyrdPreviousLogFile();
    if (previous.existsAsFile())
        previous.moveFileTo(folder.getChildFile(
            "log-" + previous.getLastModificationTime().formatted("%Y-%m-%d_%H%M%S") + ".txt"));

    inkwyrdPruneLogArchive(folder, kLogRunsKept);

    // The single log.txt this app wrote before logs moved into their own
    // folder. Moved rather than left behind, so a tester who upgrades
    // doesn't attach a stale file from the old location.
    auto legacy = folder.getParentDirectory().getChildFile("log.txt");
    if (legacy.existsAsFile())
        legacy.moveFileTo(folder.getChildFile("log-before-upgrade.txt"));
}

inline void logLine(const juce::String& message)
{
    static std::mutex logMutex;
    const std::lock_guard<std::mutex> lock(logMutex);

    auto safe = juce::String(inkwyrd::redactSecrets(message.toStdString()));
    std::cout << safe << std::endl;

    inkwyrdRotateLogsOnce();
    inkwyrdLogFile().appendText(safe + "\n");
}

// The crash path, and ONLY the crash path.
//
// Deliberately does not take logLine's mutex: this runs from an
// unhandled-exception filter, in a process that is already broken, and
// the thread that crashed may well have been holding that lock when it
// died. Blocking there would turn a crash that writes a usable log into
// a hang that writes nothing. A torn line in the log is a far better
// outcome, and in practice the process is about to stop anyway.
//
// No redaction either - a stack trace is addresses and symbol names, and
// doing less work in a crashed process is worth more here.
inline void logCrashLine(const juce::String& message)
{
    inkwyrdRotateLogsOnce();
    inkwyrdLogFile().appendText(message + "\n");
}
