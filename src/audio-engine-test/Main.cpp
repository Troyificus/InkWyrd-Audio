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

#include <juce_gui_basics/juce_gui_basics.h>

#include "PlaylistEngine.h"
#include "PlaylistLibrary.h"
#include "PlaylistPanel.h"
#include "SoundboardLayout.h"
#include "SoundboardGridComponent.h"
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

        {
            // Editing the list that's already playing (files dropped in, a
            // linked folder re-scanned). Synthetic paths: none of this
            // touches audio, and start() sets currentTrackFile whether or
            // not the file is readable, so the ordering rules can be
            // asserted without depending on the test music folder.
            juce::Array<juce::File> four, five, withoutSecond;
            for (auto* name : { "a.wav", "b.wav", "c.wav", "d.wav" })
                four.add(scratch.getChildFile(name));

            five = four;
            five.add(scratch.getChildFile("e.wav"));

            withoutSecond = four;
            withoutSecond.remove(1);

            auto relativeOrderPreserved = [](const juce::Array<juce::File>& subset,
                                              const juce::Array<juce::File>& order)
            {
                int last = -1;
                for (const auto& file : subset)
                {
                    auto index = order.indexOf(file);
                    if (index < 0 || index <= last)
                        return false;
                    last = index;
                }
                return true;
            };

            {
                PlaylistEngine engine(formatManager);
                engine.setShuffle(false);
                engine.setTracks(four);
                engine.start(); // currentTrackFile = four[0], cursor now at 1

                check(engine.getNextOrderIndex() == 1, "start() advances the play cursor past the first track");

                engine.setTracks(five);
                check(engine.getNextOrderIndex() == 0,
                       "setTracks() resets to the top of the order (the deliberate list-switch behaviour)");

                engine.setTracks(four);
                engine.start();
                engine.updateTracksPreservingOrder(five);
                check(engine.getPlayOrder() == five,
                       "unshuffled in-place edit takes the playlist's own order");
                check(engine.getNextOrderIndex() == 1,
                       "unshuffled in-place edit resumes after the playing track, not from the top");

                engine.updateTracksPreservingOrder(withoutSecond);
                check(engine.getNumTracks() == withoutSecond.size(),
                       "a track removed from the playlist leaves the play order");
                check(engine.getNextOrderIndex() == 1,
                       "removing a LATER track doesn't move the play cursor");
            }

            {
                PlaylistEngine engine(formatManager);
                engine.setShuffle(true);
                engine.setTracks(four);
                engine.start();

                auto before = engine.getPlayOrder();
                juce::Array<juce::File> stillToPlay;
                for (int i = 1; i < before.size(); ++i)
                    stillToPlay.add(before[i]);

                engine.updateTracksPreservingOrder(five);
                check(engine.getNumTracks() == 5, "shuffled in-place edit adds the new track");
                check(engine.getPlayOrder()[0] == before[0],
                       "shuffled in-place edit leaves the already-played part of the order alone");
                check(relativeOrderPreserved(stillToPlay, engine.getPlayOrder()),
                       "shuffled in-place edit does NOT re-randomise what's still to play");
                check(engine.getPlayOrder().indexOf(scratch.getChildFile("e.wav")) >= 1,
                       "a track added mid-session is spliced into the part that hasn't played yet");

                // Drop the track that already played: everything left is
                // still to come, so the cursor has to slide back to 0.
                juce::Array<juce::File> withoutPlayed = five;
                withoutPlayed.removeFirstMatchingValue(before[0]);
                engine.updateTracksPreservingOrder(withoutPlayed);
                check(engine.getNextOrderIndex() == 0,
                       "removing an already-played track moves the cursor back with it");
            }
        }

        {
            // Explorer drag-and-drop, driven through the real
            // PlaylistPanel. JUCE hands filesDropped() coordinates in the
            // TARGET component's own space and finds that target by
            // walking UP from the component under the pointer (both
            // confirmed by reading juce_ComponentPeer.cpp, not assumed),
            // so calling it directly with panel-local coordinates
            // exercises everything except the OS's own drag handoff.
            auto notAudio = scratch.getChildFile("notes.txt");
            notAudio.replaceWithText("not an audio file");

            PlaylistLibrary dropLibrary(formatManager);
            dropLibrary.setDirectory(scratch.getChildFile("drop"));
            dropLibrary.loadAll();

            auto alphaId = dropLibrary.createPlaylist("Alpha").id;
            auto betaId = dropLibrary.createPlaylist("Beta").id;

            PlaylistEngine dropEngine(formatManager);
            dropEngine.setShuffle(false);

            juce::Uuid editedId;
            int editCount = 0;

            PlaylistPanel panel(dropLibrary, dropEngine,
                                 [](const juce::Uuid&) {},
                                 [&](const juce::Uuid& id)
            {
                editedId = id;
                ++editCount;

                // Exactly what InkwyrdAudioApplication::handlePlaylistEdited
                // does, so the whole chain is under test and not just the
                // panel's half of it.
                if (auto* edited = dropLibrary.findById(id))
                    dropEngine.updateTracksPreservingOrder(dropLibrary.resolve(*edited).files);
            });

            panel.setSize(440, 700);

            check(panel.isInterestedInFileDrag({ folderTracks[0].getFullPathName() }),
                   "a dragged audio file is accepted");
            check(panel.isInterestedInFileDrag({ musicFolder.getFullPathName() }),
                   "a dragged folder is accepted");
            check(!panel.isInterestedInFileDrag({ notAudio.getFullPathName() }),
                   "a drag of only non-audio files is refused outright");

            // The panel selects the first playlist on construction, so a
            // drop that lands away from the playlist rows goes to Alpha.
            juce::StringArray mixedDrop;
            mixedDrop.add(folderTracks[0].getFullPathName());
            mixedDrop.add(notAudio.getFullPathName());
            panel.filesDropped(mixedDrop, 100, 400);

            check(dropLibrary.findById(alphaId)->entries.size() == 1,
                   "a drop away from the playlist rows goes to the SELECTED playlist");
            check(dropLibrary.resolve(*dropLibrary.findById(alphaId)).files.size() == 1,
                   "a non-audio file in the drop is filtered out, not added");
            check(editedId == alphaId && editCount == 1,
                   "the drop reports which playlist changed, once");
            check(dropEngine.getPlayOrder() == juce::Array<juce::File>({ folderTracks[0] }),
                   "the dropped track reaches the engine, not just the playlist file");

            // Row 1 of the playlist list: caption (22px) then the list
            // box, 24px rows - so y=58 is the second row, Beta, which is
            // NOT the selected one.
            panel.filesDropped({ folderTracks[1].getFullPathName() }, 100, 58);

            check(dropLibrary.findById(betaId)->entries.size() == 1,
                   "a drop onto a playlist row goes to THAT playlist, not the selected one");
            check(dropLibrary.findById(alphaId)->entries.size() == 1,
                   "dropping onto another row leaves the previously edited playlist alone");
            check(editedId == betaId, "the second drop reports the row it landed on");

            // The engine follows the playlist it's actually playing.
            check(dropEngine.getPlayOrder() == juce::Array<juce::File>({ folderTracks[1] }),
                   "editing the playlist being played pushes the new track list into the engine");

            {
                PlaylistLibrary reloaded(formatManager);
                reloaded.setDirectory(scratch.getChildFile("drop"));
                reloaded.loadAll();
                auto* beta = reloaded.findById(betaId);
                check(beta != nullptr && reloaded.resolve(*beta).files.size() == 1,
                       "a dropped file is saved to disk immediately, not just held in memory");
            }
        }

        {
            // Play/Stop. The app had no way to stop playback at all - the
            // only controls were Skip and Shuffle - so a playlist that
            // started on launch could not be silenced except by quitting.
            PlaylistEngine engine(formatManager);
            engine.setShuffle(false);

            engine.resume();
            check(!engine.isPlaying(), "Play on an empty playlist does nothing rather than half-starting");

            engine.setTracks(folderTracks);
            engine.resume();
            check(engine.isPlaying(), "Play starts from the top when nothing has been loaded yet");

            auto playing = engine.getCurrentTrackFile();
            engine.pause();
            check(!engine.isPlaying(), "Stop actually stops playback");
            check(engine.getCurrentTrackFile() == playing,
                   "Stop keeps your place rather than forgetting the track");

            engine.resume();
            check(engine.isPlaying() && engine.getCurrentTrackFile() == playing,
                   "Play after Stop carries on with the same track, it doesn't jump elsewhere");

            // Stopping mid-crossfade has to settle both decks, or resuming
            // comes back with two tracks stuck at partial volume.
            engine.skipToNext();
            check(engine.isCrossfading(), "skip starts a crossfade (setup for the next check)");
            engine.pause();
            check(!engine.isCrossfading(), "Stop during a crossfade collapses it instead of freezing it");
            check(!engine.isPlaying(), "Stop during a crossfade really stops both decks");

            engine.resume();
            check(engine.isPlaying(), "Play works again after stopping mid-crossfade");
            engine.stop();
        }

        {
            // The assignable Stream-Deck-style soundboard.
            auto boardFile = scratch.getChildFile("soundboard.json");

            SoundboardLayout board(formatManager);
            board.setFile(boardFile);
            board.load();

            check(board.getNumSlots() == SoundboardLayout::kDefaultSlotCount,
                   "a soundboard with no saved file starts as a full grid of empty buttons");
            check(board.getSlot(0).isEmpty(), "buttons start empty");

            check(board.assign(0, folderTracks[0]), "a playable file can be assigned to a button");
            check(board.getSlot(0).name == folderTracks[0].getFileNameWithoutExtension(),
                   "a button's default name is the filename, which is what Stream Deck buttons send");
            check(!board.assign(1, scratch.getChildFile("notes.txt")),
                   "a non-audio file is refused rather than making a dead button");
            check(board.getSlot(1).isEmpty(), "a refused assignment leaves the button alone");

            // The same filename on two buttons would collide in the
            // engine's name-keyed map, hiding one of them.
            check(board.assign(1, folderTracks[0]), "the same sound can go on two buttons");
            check(board.getSlot(1).name != board.getSlot(0).name,
                   "a duplicate button name is auto-suffixed, so both stay triggerable");

            check(!board.rename(1, board.getSlot(0).name),
                   "renaming a button onto another button's name is refused");
            check(!board.rename(1, "   "), "renaming a button to nothing is refused");
            check(board.rename(1, "Thunder"), "renaming to a free name succeeds");

            check(board.assign(SoundboardLayout::kDefaultSlotCount + 2, folderTracks[1]),
                   "assigning past the end of the board grows it");
            check(board.getNumSlots() == SoundboardLayout::kDefaultSlotCount + 3,
                   "the board grows exactly far enough to hold the new button");

            auto beforeShrink = board.getNumSlots();
            check(board.setNumSlots(1) == SoundboardLayout::kDefaultSlotCount + 3,
                   "shrinking never removes a button that has a sound on it");
            check(board.getNumSlots() == beforeShrink, "the refused shrink changed nothing");

            board.setColour(0, 0xff8c2f2f);

            {
                SoundboardLayout reloaded(formatManager);
                reloaded.setFile(boardFile);
                reloaded.load();

                check(reloaded.getNumSlots() == board.getNumSlots(), "board size survives a round-trip");
                check(reloaded.getSlot(0).file == folderTracks[0]
                       && reloaded.getSlot(1).name == "Thunder",
                       "button assignments and names survive a round-trip");
                check(reloaded.getSlot(0).colourArgb == 0xff8c2f2f, "button colour survives a round-trip");
                check(reloaded.getSlot(SoundboardLayout::kDefaultSlotCount + 2).file == folderTracks[1],
                       "a button's POSITION survives a round-trip, not just its existence");
                check(reloaded.getFilledSlots().size() == 3, "only the assigned buttons are stored");
            }

            {
                // Importing must not duplicate or rearrange a board that
                // someone has already set up by hand.
                SoundboardLayout importer(formatManager);
                importer.setFile(scratch.getChildFile("import.json"));
                importer.load();

                check(importer.importFolder(musicFolder) > 0, "importing a folder fills buttons");
                check(importer.importFolder(musicFolder) == 0,
                       "importing the same folder twice adds nothing - no duplicate buttons");

                auto keptInPlace = importer.getSlot(1).file;
                importer.clearSlot(0);
                check(importer.importFolder(musicFolder) == 1,
                       "re-importing after clearing one button restores just that one");
                check(importer.getSlot(1).file == keptInPlace,
                       "re-importing doesn't rearrange the buttons around it");
            }

            {
                // A newer file must be left strictly alone, same rule as
                // the playlist files.
                auto futureFile = scratch.getChildFile("future-board.json");
                futureFile.replaceWithText("{ \"schemaVersion\": 99, \"slotCount\": 4 }");
                auto before = futureFile.loadFileAsString();

                SoundboardLayout future(formatManager);
                future.setFile(futureFile);
                future.load();

                check(!future.getLoadWarnings().isEmpty(), "a newer board schemaVersion is reported");
                check(futureFile.loadFileAsString() == before,
                       "a newer board file is left untouched on disk");

                auto brokenFile = scratch.getChildFile("broken-board.json");
                brokenFile.replaceWithText("{ this is not json");
                SoundboardLayout broken(formatManager);
                broken.setFile(brokenFile);
                broken.load();
                check(!broken.getLoadWarnings().isEmpty(),
                       "an unreadable board file is reported rather than crashing");
            }

            {
                // Exactly what InkwyrdAudioApplication does on startup:
                // migrate the old sound-effects folder onto the board,
                // then register every filled slot with the engine. The
                // acceptance criterion for this whole change is that a
                // Stream Deck button, which triggers by NAME, keeps
                // working across the migration with the shipped plugin
                // unmodified.
                SoundboardLayout startupBoard(formatManager);
                startupBoard.setFile(scratch.getChildFile("startup-board.json"));
                startupBoard.load();
                startupBoard.importFolder(musicFolder);

                SoundboardEngine startupEngine(formatManager);
                startupEngine.clearSounds();
                for (const auto& slot : startupBoard.getFilledSlots())
                    if (slot.file.existsAsFile())
                        startupEngine.registerSound(slot.name, slot.file);

                check(startupEngine.getRegisteredNames().size() == folderTracks.size(),
                       "every migrated sound is registered with the engine");

                bool everyOldNameStillWorks = true;
                for (const auto& track : folderTracks)
                    if (!startupEngine.hasSound(track.getFileNameWithoutExtension()))
                        everyOldNameStillWorks = false;

                check(everyOldNameStillWorks,
                       "migrated buttons keep their old filename-based names, so existing Stream Deck "
                       "buttons still match");

                // A sound whose file has gone must stay ON the board (so
                // the user can see what happened and fix it) but must NOT
                // be registered, so pressing it is a no-op rather than a
                // failed read mid-session.
                auto vanishing = scratch.getChildFile("vanishing.wav");
                folderTracks[0].copyFileTo(vanishing);

                SoundboardLayout missingBoard(formatManager);
                missingBoard.setFile(scratch.getChildFile("missing-board.json"));
                missingBoard.load();
                missingBoard.assign(0, vanishing);
                vanishing.deleteFile();

                SoundboardLayout afterDelete(formatManager);
                afterDelete.setFile(scratch.getChildFile("missing-board.json"));
                afterDelete.load();
                check(!afterDelete.getSlot(0).isEmpty(),
                       "a button whose file has gone stays on the board rather than vanishing");

                SoundboardEngine missingEngine(formatManager);
                for (const auto& slot : afterDelete.getFilledSlots())
                    if (slot.file.existsAsFile())
                        missingEngine.registerSound(slot.name, slot.file);

                check(missingEngine.getRegisteredNames().isEmpty(),
                       "a button whose file has gone is not registered, so pressing it does nothing");
            }

            {
                // Dropping files onto a SPECIFIC button, through the real
                // grid component. The width is forced narrow so the grid
                // is one column and a row number IS a slot number, making
                // the coordinate maths deterministic rather than
                // dependent on the window size.
                SoundboardLayout dropBoard(formatManager);
                dropBoard.setFile(scratch.getChildFile("drop-board.json"));
                dropBoard.load();

                SoundboardEngine dropEngine(formatManager);
                int layoutChanges = 0;

                SoundboardGridComponent grid(dropEngine, dropBoard, [&] { ++layoutChanges; });
                grid.setSize(200, 600);

                // Caption row (26) + hint (18) + 4 gap, then 72px cells
                // with 6px gaps - so row r is centred at 48 + r*78 + 36.
                auto yForSlot = [](int slot) { return 48 + slot * 78 + 36; };

                check(grid.isInterestedInFileDrag({ folderTracks[0].getFullPathName() }),
                       "the board accepts a dragged file");

                grid.filesDropped({ folderTracks[0].getFullPathName() }, 20, yForSlot(1));
                check(dropBoard.getSlot(1).file == folderTracks[0],
                       "a file dropped on a button lands on THAT button");
                check(dropBoard.getSlot(0).isEmpty(),
                       "a drop doesn't spill onto the buttons before it");
                check(layoutChanges == 1, "a drop tells the app to re-register the sounds");

                grid.filesDropped({ folderTracks[1].getFullPathName(),
                                     folderTracks[2].getFullPathName() },
                                   20, yForSlot(0));

                check(dropBoard.getSlot(0).file == folderTracks[1],
                       "dropping several files starts at the button aimed at");
                check(dropBoard.getSlot(1).file == folderTracks[0],
                       "the rest of a multi-file drop skips buttons already in use");
                check(dropBoard.getSlot(2).file == folderTracks[2],
                       "the rest of a multi-file drop fills the next FREE button");
            }
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

    // TEMPORARY diagnostic: INKWYRD_TIMELOAD="fileA|fileB" measures how
    // long PlaylistEngine blocks the calling (message) thread when it
    // loads a track, and how much SILENCE the incoming deck produces
    // before its read-ahead buffer has anything in it. Both map directly
    // onto "the app froze and the audio paused" at a crossfade.
    {
        auto timeLoad = juce::SystemStats::getEnvironmentVariable("INKWYRD_TIMELOAD", "");
        if (timeLoad.isNotEmpty())
        {
            juce::ScopedJuceInitialiser_GUI juceInitialiser;
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            fm.registerFormat(new Mp3AudioFormat(), false);
            fm.registerFormat(new MediaFoundationAudioFormat(), false);

            juce::StringArray paths;
            paths.addTokens(timeLoad, "|", "");

            juce::Array<juce::File> files;
            for (const auto& path : paths)
                files.add(juce::File(path.trim()));

            for (const auto& f : files)
                std::cout << "file: " << f.getFileName() << "  exists=" << (f.existsAsFile() ? "yes" : "no")
                           << "  " << (f.getSize() / (1024 * 1024)) << " MB" << std::endl;

            // Raw reader cost, outside the engine.
            for (const auto& f : files)
            {
                auto t0 = juce::Time::getMillisecondCounterHiRes();
                std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(f));
                auto t1 = juce::Time::getMillisecondCounterHiRes();
                std::cout << "createReaderFor(" << f.getFileName() << ") = "
                           << juce::String(t1 - t0, 1) << " ms"
                           << (reader != nullptr ? "" : "  [FAILED]") << std::endl;
            }

            const int blockSize = 480;      // ~10 ms at 48k, like a real device
            const double sr = 48000.0;

            PlaylistEngine engine(fm);
            engine.setShuffle(false);
            engine.setTracks(files);
            engine.prepareToPlay(blockSize, sr);

            auto t0 = juce::Time::getMillisecondCounterHiRes();
            engine.start();
            auto t1 = juce::Time::getMillisecondCounterHiRes();
            std::cout << "engine.start() blocked the calling thread for "
                       << juce::String(t1 - t0, 1) << " ms" << std::endl;

            juce::AudioBuffer<float> buffer(2, blockSize);
            juce::AudioSourceChannelInfo info(&buffer, 0, blockSize);

            // Pull blocks at real time and report how long output stays
            // silent - that IS the audible gap.
            auto pullUntilAudible = [&](const char* what)
            {
                int silentBlocks = 0;
                for (int i = 0; i < 500; ++i) // up to 5 s
                {
                    buffer.clear();
                    engine.getNextAudioBlock(info);

                    if (buffer.getMagnitude(0, blockSize) > 0.0001f)
                        break;

                    ++silentBlocks;
                    juce::Thread::sleep(10); // let the read-ahead thread work, as a real device would
                }

                std::cout << what << ": " << juce::String(silentBlocks * blockSize * 1000.0 / sr, 0)
                           << " ms of silence before audio appeared" << std::endl;
            };

            pullUntilAudible("after start(), pulled immediately");

            // Keep pulling for a second so playback is settled.
            for (int i = 0; i < 100; ++i) { buffer.clear(); engine.getNextAudioBlock(info); juce::Thread::sleep(10); }

            auto t2 = juce::Time::getMillisecondCounterHiRes();
            engine.skipToNext();
            auto t3 = juce::Time::getMillisecondCounterHiRes();
            std::cout << "engine.skipToNext() blocked the calling thread for "
                       << juce::String(t3 - t2, 1) << " ms" << std::endl;

            // Does a HEAD START fix it? This is the whole question behind
            // preloading the next deck before the crossfade rather than at
            // it: give the same source time to buffer with nothing being
            // pulled from it, then see whether it plays cleanly.
            for (auto warmUpMs : { 0, 250, 500, 1000, 2000 })
            {
                PlaylistEngine warm(fm);
                warm.setShuffle(false);
                warm.setTracks(files);
                warm.prepareToPlay(blockSize, sr);
                warm.start();

                juce::Thread::sleep(warmUpMs); // nothing pulled: pure buffering time

                int silentBlocks = 0;
                for (int i = 0; i < 300; ++i)
                {
                    buffer.clear();
                    warm.getNextAudioBlock(info);
                    if (buffer.getMagnitude(0, blockSize) > 0.0001f)
                        break;
                    ++silentBlocks;
                    juce::Thread::sleep(10);
                }

                std::cout << "warm-up " << warmUpMs << " ms -> "
                           << juce::String(silentBlocks * blockSize * 1000.0 / sr, 0)
                           << " ms of silence" << std::endl;

                warm.stop();
                warm.releaseResources();
            }

            engine.stop();
            engine.releaseResources();
            return 0;
        }
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
