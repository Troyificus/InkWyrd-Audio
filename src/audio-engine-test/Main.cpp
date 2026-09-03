#include <cstdlib>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <crtdbg.h>
#endif

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "PlaylistEngine.h"
#include "PlaylistLibrary.h"
#include "SoundboardEngine.h"
#include "Mp3AudioFormat.h"
#include "MediaFoundationAudioFormat.h"

namespace
{
    // Headless checks for PlaylistLibrary, run with INKWYRD_SELFTEST=1.
    // Pure logic - no audio device, no message loop - so this is a
    // repeatable regression test rather than a listen-and-judge one.
    int failures = 0;

    void check(bool condition, const juce::String& what)
    {
        std::cout << (condition ? "  PASS  " : "  FAIL  ") << what << std::endl;
        if (!condition)
            ++failures;
    }

    int runSelfTest(juce::AudioFormatManager& formatManager, const juce::File& musicFolder)
    {
        auto scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("inkwyrd-playlist-selftest");
        scratch.deleteRecursively();
        scratch.createDirectory();
        std::cout << "Self-test scratch dir: " << scratch.getFullPathName() << std::endl;

        auto folderTracks = inkwyrd::scanFolderForAudio(musicFolder, formatManager, true);
        std::cout << "Music folder has " << folderTracks.size() << " playable file(s)" << std::endl;
        check(!folderTracks.isEmpty(), "test music folder is not empty (everything below depends on this)");
        if (folderTracks.isEmpty())
            return 1;

        {
            PlaylistLibrary library(formatManager);
            library.setDirectory(scratch);
            library.loadAll();
            check(library.isEmpty(), "empty directory loads as an empty library");

            auto& live = library.createPlaylist("Ambient");
            library.addFolderLink(live.id, musicFolder, true);
            check(library.resolve(live).files.size() == folderTracks.size(),
                   "live folder link resolves to every file in the folder");

            // Adding a file that's already inside the linked folder must
            // not double it up.
            library.addFiles(live.id, { folderTracks[0] });
            check(library.resolve(live).files.size() == folderTracks.size(),
                   "a file already inside a linked folder is de-duplicated");

            auto& snap = library.createPlaylist("Battle");
            library.addFolderSnapshot(snap.id, musicFolder, true);
            check(library.resolve(snap).files.size() == folderTracks.size(),
                   "snapshot import captures the folder's files");

            auto& dupe = library.createPlaylist("Ambient");
            check(dupe.name == "Ambient (2)", "duplicate playlist name is auto-suffixed");

            auto& missing = library.createPlaylist("Broken");
            library.addFiles(missing.id, { folderTracks[0] });
            missing.entries.getReference(0).path = musicFolder.getChildFile("does-not-exist.wav");
            auto resolvedMissing = library.resolve(missing);
            check(resolvedMissing.files.isEmpty() && resolvedMissing.missingPaths.size() == 1,
                   "a missing file is reported, not silently dropped");

            library.setShuffle(live.id, false);
            check(!live.shuffle, "shuffle is stored per playlist");
        }

        {
            // Fresh library over the same directory: everything above must
            // survive the JSON round-trip.
            PlaylistLibrary reloaded(formatManager);
            reloaded.setDirectory(scratch);
            reloaded.loadAll();

            check(reloaded.getNumPlaylists() == 4, "all four playlists reload from disk");

            auto* live = reloaded.findByName("Ambient");
            check(live != nullptr, "playlist is found by name after reload");

            if (live != nullptr)
            {
                check(!live->shuffle, "per-playlist shuffle survived the round-trip");
                check(reloaded.resolve(*live).files.size() == folderTracks.size(),
                       "live folder link still resolves after reload");
            }

            auto* snap = reloaded.findByName("Battle");
            check(snap != nullptr && snap->entries.size() == 1
                      && !snap->entries.getReference(0).live
                      && snap->entries.getReference(0).snapshot.size() == folderTracks.size(),
                   "snapshot entry round-trips with its frozen track list");

            if (live != nullptr)
            {
                auto id = live->id;
                check(reloaded.findById(id) != nullptr, "playlist id survives the round-trip");
                check(reloaded.renamePlaylist(id, "Battle") == false,
                       "renaming onto an existing name is refused");
                check(reloaded.renamePlaylist(id, "Tavern"), "renaming to a free name succeeds");
            }
        }

        {
            // A file from a newer version must be left strictly alone.
            auto future = scratch.getChildFile("from-the-future.json");
            future.replaceWithText("{ \"schemaVersion\": 99, \"name\": \"Future\", \"entries\": [] }");
            auto before = future.loadFileAsString();

            PlaylistLibrary library(formatManager);
            library.setDirectory(scratch);
            library.loadAll();

            check(library.findByName("Future") == nullptr, "a newer schemaVersion is not loaded");
            check(!library.getLoadWarnings().isEmpty(), "a newer schemaVersion is reported as a warning");
            check(future.loadFileAsString() == before, "a newer schemaVersion file is left untouched on disk");

            auto garbage = scratch.getChildFile("garbage.json");
            garbage.replaceWithText("this is not json {{{");
            library.loadAll();
            check(library.getLoadWarnings().size() >= 2, "unparseable JSON is reported rather than crashing");
        }

        {
            // Engine-level guarantees that don't need an audio device.
            // A mis-click must never be able to leave the room silent.
            PlaylistEngine engine(formatManager);
            engine.setShuffle(false);
            engine.setTracks(folderTracks);
            check(engine.getNumTracks() == folderTracks.size(), "setTracks populates the play order");

            engine.crossfadeToTracks({});
            check(engine.getNumTracks() == folderTracks.size(),
                   "crossfading to an EMPTY list is refused, leaving the current list intact");

            juce::Array<juce::File> single;
            single.add(folderTracks[0]);
            engine.crossfadeToTracks(single);
            check(engine.getNumTracks() == 1, "crossfading to a non-empty list swaps the play order");

            engine.crossfadeToTrackInCurrentList(folderTracks[folderTracks.size() - 1]);
            check(engine.getNumTracks() == 1,
                   "jumping to a track that isn't in the current list is a no-op");

            bool shuffleCallbackFired = false;
            engine.setShuffleChangedCallback([&](bool) { shuffleCallbackFired = true; });
            engine.setShuffle(true);
            check(shuffleCallbackFired, "shuffle change notifies (so per-playlist shuffle can persist)");

            shuffleCallbackFired = false;
            engine.setShuffle(true);
            check(!shuffleCallbackFired, "setting shuffle to its existing value doesn't re-notify");
        }

        scratch.deleteRecursively();

        std::cout << (failures == 0 ? "SELF-TEST PASSED" : "SELF-TEST FAILED")
                   << " (" << failures << " failure(s))" << std::endl;
        return failures == 0 ? 0 : 1;
    }

    juce::StringArray registerSoundboardFolder(SoundboardEngine& soundboard,
                                                juce::AudioFormatManager& formatManager,
                                                const juce::File& folder)
    {
        juce::StringArray names;
        for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFiles))
        {
            auto file = entry.getFile();
            if (formatManager.findFormatForFileExtension(file.getFileExtension()) == nullptr)
                continue;
            auto name = file.getFileNameWithoutExtension();
            soundboard.registerSound(name, file);
            names.add(name);
        }
        return names;
    }
}

int main(int argc, char* argv[])
{
    juce::ignoreUnused(argc, argv);

#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    auto playlistFolder = juce::SystemStats::getEnvironmentVariable("PLAYLIST_FOLDER", "");
    auto soundboardFolder = juce::SystemStats::getEnvironmentVariable("SOUNDBOARD_FOLDER", "");

    if (playlistFolder.isEmpty())
    {
        std::cout << "Set PLAYLIST_FOLDER (and optionally SOUNDBOARD_FOLDER) env vars first." << std::endl;
        return 1;
    }

    if (juce::SystemStats::getEnvironmentVariable("INKWYRD_SELFTEST", "").isNotEmpty())
    {
        juce::ScopedJuceInitialiser_GUI juceInitialiser; // JUCE types need this even headless
        juce::AudioFormatManager selfTestFormats;
        selfTestFormats.registerBasicFormats();
        selfTestFormats.registerFormat(new Mp3AudioFormat(), false);
        selfTestFormats.registerFormat(new MediaFoundationAudioFormat(), false);
        return runSelfTest(selfTestFormats, juce::File(playlistFolder));
    }

    // PlaylistEngine's crossfade timing runs on a juce::Timer, which is
    // only ever dispatched by an active JUCE message loop pumped from
    // whichever thread JUCE considers "the" message thread - normally
    // the thread that first initialises MessageManager. That has to be
    // this (main) thread, so interactive stdin reading runs on a
    // background thread instead, with playlist/soundboard calls
    // marshalled back via callAsync to avoid racing the Timer callback.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats(); // WAV/AIFF/FLAC/Ogg Vorbis
    formatManager.registerFormat(new Mp3AudioFormat(), false);
    formatManager.registerFormat(new MediaFoundationAudioFormat(), false); // AAC/M4A + WMA

    PlaylistEngine playlist(formatManager);
    SoundboardEngine soundboard(formatManager);

    juce::StringArray soundNames;
    if (soundboardFolder.isNotEmpty())
        soundNames = registerSoundboardFolder(soundboard, formatManager, juce::File(soundboardFolder));

    juce::MixerAudioSource masterMixer;
    masterMixer.addInputSource(&playlist, false);
    masterMixer.addInputSource(&soundboard, false);

    juce::AudioSourcePlayer sourcePlayer;
    sourcePlayer.setSource(&masterMixer);

    juce::AudioDeviceManager deviceManager;
    auto openError = deviceManager.initialiseWithDefaultDevices(0, 2); // no inputs, stereo out
    if (openError.isNotEmpty())
    {
        std::cout << "Failed to open audio device: " << openError << std::endl;
        return 1;
    }
    deviceManager.addAudioCallback(&sourcePlayer);

    playlist.loadFolder(juce::File(playlistFolder));
    playlist.start();

    std::cout << "Playing from: " << playlistFolder << std::endl;
    std::cout << "Commands: s = skip/crossfade to next track, h = toggle shuffle, t = now playing";
    if (!soundNames.isEmpty())
    {
        std::cout << ", soundboard:";
        for (int i = 0; i < soundNames.size(); ++i)
            std::cout << " " << i << "=" << soundNames[i];
    }
    std::cout << ", q = quit" << std::endl;

    bool shuffle = true; // only ever touched from callAsync lambdas below, all on the message thread

    std::thread inputThread([&]
    {
        std::string line;
        while (true)
        {
            std::cout << "> ";
            if (!std::getline(std::cin, line) || line == "q")
            {
                // Ends runDispatchLoop() below - safe to call from any
                // thread, that's the documented purpose of this method.
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
            }

            if (line.empty())
                continue;

            if (line == "s")
            {
                juce::MessageManager::callAsync([&playlist]
                {
                    playlist.skipToNext();
                    std::cout << "Crossfading to next track..." << std::endl;
                });
            }
            else if (line == "h")
            {
                juce::MessageManager::callAsync([&playlist, &shuffle]
                {
                    shuffle = !shuffle;
                    playlist.setShuffle(shuffle);
                    std::cout << "Shuffle: " << (shuffle ? "on" : "off")
                               << " (takes effect next time the order wraps)" << std::endl;
                });
            }
            else if (line == "t")
            {
                juce::MessageManager::callAsync([&playlist]
                {
                    std::cout << "Now playing: " << playlist.getCurrentTrackName()
                               << (playlist.isCrossfading() ? " (crossfading)" : "") << std::endl;
                });
            }
            else
            {
                auto index = std::atoi(line.c_str());
                if (index >= 0 && index < soundNames.size())
                {
                    auto name = soundNames[index];
                    juce::MessageManager::callAsync([&soundboard, name]
                    {
                        soundboard.trigger(name);
                        std::cout << "Triggered: " << name << std::endl;
                    });
                }
                else
                {
                    std::cout << "Unknown command." << std::endl;
                }
            }
        }
    });

    juce::MessageManager::getInstance()->runDispatchLoop(); // blocks until stopDispatchLoop() above

    inputThread.join();

    deviceManager.removeAudioCallback(&sourcePlayer);
    sourcePlayer.setSource(nullptr);
    playlist.stop();

    return 0;
}
