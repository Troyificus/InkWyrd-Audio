#include <cstdlib>
#include <iostream>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <crtdbg.h>
#endif

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_WINDOWS
 #include <objbase.h> // CoInitializeEx, for the tag probe's property-store reads
#endif

#include "DiscordRpcClient.h"
#include "InkwyrdLookAndFeel.h"
#include "InkwyrdTheme.h"
#include "NoiseSuppressor.h"
#include "PlaylistEngine.h"
#include "PlaylistLibrary.h"
#include "PlaylistPanel.h"
#include "LibraryFolderTree.h"
#include "PlaylistTrackListComponent.h"
#include "TrackLibrary.h"
#include "TrackMetadataStore.h"
#include "Mp3AudioFormat.h"
#include "MediaFoundationAudioFormat.h"
#include "SoundboardLayout.h"
#include "TrackSettingsStore.h"
#include "SoundboardGridComponent.h"
#include "VolumeCallout.h"
#include "SoundboardEngine.h"
#include "WindowSnapping.h"
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
            //
            // A drop now lands in the master TRACK LIBRARY, not on a
            // playlist row. Playlists are assembled FROM the library
            // (drag onto the Playlist window, or "Add to playlist"), so a
            // drop that silently edited whichever playlist happened to be
            // under the pointer would be the odd one out.
            auto notAudio = scratch.getChildFile("notes.txt");
            notAudio.replaceWithText("not an audio file");

            PlaylistLibrary dropLibrary(formatManager);
            dropLibrary.setDirectory(scratch.getChildFile("drop"));
            dropLibrary.loadAll();

            auto alphaId = dropLibrary.createPlaylist("Alpha").id;

            PlaylistEngine dropEngine(formatManager);
            dropEngine.setShuffle(false);

            TrackSettingsStore dropGains;
            dropGains.setFile(scratch.getChildFile("drop-gains.json"));

            TrackLibrary dropTracks;
            dropTracks.setFile(scratch.getChildFile("drop-track-library.json"));
            dropTracks.load();

            // Left unloaded: these checks are about drag-and-drop, and an
            // empty metadata store makes every row fall back to its
            // filename, which is exactly what they assert on.
            TrackMetadataStore dropMetadata;

            PlaylistPanel panel(dropLibrary, dropTracks, dropEngine, dropGains, dropMetadata,
                                 [](const juce::Uuid&) {},
                                 [](const juce::Uuid&) {},
                                 [](const juce::Uuid&) {});

            panel.setSize(440, 700);

            check(panel.isInterestedInFileDrag({ folderTracks[0].getFullPathName() }),
                   "a dragged audio file is accepted");
            check(panel.isInterestedInFileDrag({ musicFolder.getFullPathName() }),
                   "a dragged folder is accepted");
            check(!panel.isInterestedInFileDrag({ notAudio.getFullPathName() }),
                   "a drag of only non-audio files is refused outright");

            juce::StringArray mixedDrop;
            mixedDrop.add(folderTracks[0].getFullPathName());
            mixedDrop.add(notAudio.getFullPathName());
            panel.filesDropped(mixedDrop, 100, 400);

            check(dropTracks.getNumTracks() == 1, "a dropped audio file lands in the track library");
            check(dropTracks.contains(folderTracks[0]), "and it's the file that was actually dropped");
            check(!dropTracks.contains(notAudio), "a non-audio file in the drop is filtered out, not added");
            check(dropLibrary.findById(alphaId)->entries.isEmpty(),
                   "a drop does NOT quietly edit a playlist - the library is what receives it");

            // Dropping the whole folder brings in everything playable,
            // and must not duplicate the file already dropped above.
            panel.filesDropped({ musicFolder.getFullPathName() }, 100, 400);
            check(dropTracks.getNumTracks() == folderTracks.size(),
                   "dropping a folder adds every playable file in it, without duplicating one already there");

            {
                TrackLibrary reloaded;
                reloaded.setFile(scratch.getChildFile("drop-track-library.json"));
                reloaded.load();
                check(reloaded.getNumTracks() == folderTracks.size(),
                       "a dropped file is saved to disk immediately, not just held in memory");
            }
        }

        {
            // The Playlist window's receiving half: a drop lands in the
            // playlist being SHOWN. The sending half - dragging a row out
            // of the Library window - is
            // DragAndDropContainer::performExternalDragDropOfFiles, an
            // OS-level drag loop that synthetic mouse input can't drive
            // (this project has been bitten by synthetic input before,
            // see CLAUDE.md), so it stays a by-hand check. This covers
            // everything from the drop onwards, which is where the logic
            // actually lives.
            PlaylistLibrary dropLibrary(formatManager);
            dropLibrary.setDirectory(scratch.getChildFile("track-drop"));
            dropLibrary.loadAll();

            auto targetId = dropLibrary.createPlaylist("Target").id;
            auto otherId = dropLibrary.createPlaylist("Other").id;

            PlaylistEngine dropEngine(formatManager);

            juce::Uuid editedId;
            int editCount = 0;

            TrackMetadataStore listMetadata;
            PlaylistTrackListComponent list(dropLibrary, listMetadata, dropEngine,
                                             [&](const juce::Uuid& id) { editedId = id; ++editCount; },
                                             [](const juce::Uuid&, const juce::File&) {});
            list.setSize(320, 480);

            check(! list.isInterestedInFileDrag({ folderTracks[0].getFullPathName() }),
                   "with no playlist shown there is nothing to drop into, so the drag is refused");

            list.setPlaylist(targetId);
            check(list.isInterestedInFileDrag({ folderTracks[0].getFullPathName() }),
                   "once a playlist is shown, an audio file is accepted");

            auto notAudioFile = scratch.getChildFile("not-audio.txt");
            notAudioFile.replaceWithText("nope");
            check(! list.isInterestedInFileDrag({ notAudioFile.getFullPathName() }),
                   "a non-audio drag is refused");

            juce::StringArray drop;
            drop.add(folderTracks[0].getFullPathName());
            drop.add(notAudioFile.getFullPathName());
            list.filesDropped(drop, 50, 50);

            check(dropLibrary.findById(targetId)->entries.size() == 1,
                   "a drop adds the track to the playlist being shown");
            check(dropLibrary.findById(otherId)->entries.isEmpty(),
                   "and not to any other playlist");
            check(editedId == targetId && editCount == 1,
                   "the drop reports exactly which playlist changed, once");

            {
                PlaylistLibrary reloaded(formatManager);
                reloaded.setDirectory(scratch.getChildFile("track-drop"));
                reloaded.loadAll();
                auto* target = reloaded.findById(targetId);
                check(target != nullptr && reloaded.resolve(*target).files.size() == 1,
                       "and it's on disk immediately, not just in memory");
            }
        }

        {
            // The Library window's folder tree: pure grouping over paths,
            // so it can be checked without opening a window.
            juce::Array<juce::File> paths;
            for (auto path : { "G:/Music/Black Waters/02 Alone.mp3",
                                "G:/Music/Black Waters/10 Creep.mp3",
                                "G:/Music/Black Waters/1 Seance.mp3",
                                "G:/Music/Sentinel/Abyss.mp3",
                                "G:/Music/loose.mp3",
                                "D:/Other/thing.mp3" })
                paths.add(juce::File(juce::String(path)));

            auto tree = inkwyrd::buildFolderTree(paths);

            check(tree->children.size() == 2, "tracks on two drives give two top-level folders");
            check(tree->totalTrackCount == paths.size(), "every track is counted once, at the root");

            // D: sorts before G:, and the D: side is a single chain with
            // one file, so the whole run collapses to one row.
            auto* dDrive = tree->children[0];
            check(dDrive->files.size() == 1 && dDrive->children.isEmpty(),
                   "a chain of folders with nothing branching collapses to a single row");
            check(dDrive->name.contains("Other"),
                   "and the collapsed row names the whole run rather than just the drive");

            auto* music = tree->children[1];
            check(music->children.size() == 2, "a folder with two sub-folders keeps them both");
            check(music->files.size() == 1,
                   "a folder holding both files and sub-folders keeps its own files");
            check(music->totalTrackCount == 5, "and counts everything underneath it");
            check(music->name.contains("Music"),
                   "the collapse stops where the folder actually branches");

            auto* blackWaters = music->children[0];
            check(blackWaters->name == "Black Waters", "sub-folders are named by their own folder");
            check(blackWaters->files.size() == 3, "with the tracks that are in them");
            check(blackWaters->files[0].getFileName().startsWith("1 ")
                   && blackWaters->files[1].getFileName().startsWith("02 ")
                   && blackWaters->files[2].getFileName().startsWith("10 "),
                   "tracks sort naturally, so 2 comes before 10 rather than after it");

            // Windows paths are case-insensitive, so the same folder
            // spelled two ways is one row, not two.
            juce::Array<juce::File> mixedCase;
            mixedCase.add(juce::File("G:/Music/a.mp3"));
            mixedCase.add(juce::File("g:/music/b.mp3"));
            auto caseTree = inkwyrd::buildFolderTree(mixedCase);
            check(caseTree->children.size() == 1 && caseTree->children[0]->files.size() == 2,
                   "the same folder in different case is one folder, holding both tracks");

            check(inkwyrd::buildFolderTree({})->children.isEmpty(),
                   "an empty library gives an empty tree rather than a phantom row");
        }

        {
            // The master track library on its own: dedup, persistence,
            // removal, and the newer-schema rule every other store here
            // follows.
            auto libraryFile = scratch.getChildFile("track-library-unit.json");

            TrackLibrary tracks;
            tracks.setFile(libraryFile);
            tracks.load();
            check(tracks.getNumTracks() == 0, "a fresh track library is empty rather than failing to load");

            check(tracks.registerTrack(folderTracks[0]), "registering a new track reports that it was new");
            check(!tracks.registerTrack(folderTracks[0]), "registering the same track again is a no-op");
            check(tracks.getNumTracks() == 1, "and doesn't duplicate it");

            // Windows paths are case-insensitive, so the same file
            // reached through a differently-cased path is the same track.
            juce::File sameFileOtherCase(folderTracks[0].getFullPathName().toUpperCase());
            check(!tracks.registerTrack(sameFileOtherCase),
                   "the same path in different case is recognised as the same track");
            check(tracks.getNumTracks() == 1, "so case alone never creates a second row");

            // The hook the app uses to read tags for tracks added
            // mid-session: fires for a batch with something new in it,
            // and stays quiet for one that changes nothing, so re-adding
            // a folder doesn't kick off a pointless re-scan.
            int addedNotifications = 0;
            tracks.onTracksAdded = [&] { ++addedNotifications; };

            tracks.registerTracks(folderTracks);
            check(tracks.getNumTracks() == folderTracks.size(), "registering a batch adds the rest");
            check(addedNotifications == 1, "a batch with new tracks in it announces itself, once");

            tracks.registerTracks(folderTracks);
            check(addedNotifications == 1, "a batch that adds nothing new stays quiet");
            tracks.onTracksAdded = nullptr;
            tracks.save();

            {
                TrackLibrary reloaded;
                reloaded.setFile(libraryFile);
                reloaded.load();
                check(reloaded.getNumTracks() == folderTracks.size(),
                       "the library survives a save/load round trip");
                check(reloaded.contains(folderTracks[1]), "with the right tracks in it");
            }

            tracks.removeTrack(folderTracks[0]);
            check(!tracks.contains(folderTracks[0]), "a track can be removed");
            check(tracks.getNumTracks() == folderTracks.size() - 1, "and only that one goes");

            // Sorted by track name, not insertion order or full path.
            auto all = tracks.getAllTracks();
            bool sorted = true;
            for (int i = 1; i < all.size(); ++i)
                if (all[i - 1].getFileNameWithoutExtension()
                        .compareIgnoreCase(all[i].getFileNameWithoutExtension()) > 0)
                    sorted = false;
            check(sorted, "the list comes back sorted by track name, so it's findable");

            {
                // A file from a NEWER version is reported and left alone,
                // never half-read and written back in an older shape.
                auto futureFile = scratch.getChildFile("track-library-future.json");
                futureFile.replaceWithText("{\"schemaVersion\": 99, \"tracks\": [\"nope.wav\"]}");
                auto before = futureFile.loadFileAsString();

                TrackLibrary future;
                future.setFile(futureFile);
                future.load();
                check(future.getNumTracks() == 0, "a newer schemaVersion is skipped rather than misread");
                check(!future.getLoadWarnings().isEmpty(), "and reported rather than silently ignored");
                check(futureFile.loadFileAsString() == before, "the newer file is left untouched on disk");
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
            // Hard Stop, Fade out, and turning crossfade off.
            PlaylistEngine engine(formatManager);
            engine.setShuffle(false);
            engine.setTracks(folderTracks);
            engine.resume();

            engine.skipToNext();
            auto secondTrack = engine.getCurrentTrackFile();
            juce::ignoreUnused(secondTrack);

            engine.hardStop();
            check(!engine.isPlaying(), "Stop silences playback");
            check(engine.getCurrentTrackFile() == juce::File(),
                   "Stop forgets where it was - that is what makes it a stop and not a pause");
            check(!engine.isCrossfading(), "Stop collapses a crossfade rather than leaving it running");

            engine.resume();
            check(engine.isPlaying() && engine.getCurrentTrackFile() == folderTracks[0],
                   "Play after Stop begins the list again from the top");

            // Fade out
            check(!engine.isFadingOut(), "nothing is fading before the button is pressed");
            engine.fadeOutAndStop(2.0);
            check(engine.isFadingOut(), "Fade out starts a fade");
            check(engine.isPlaying(), "a fade-out is still playing while it fades");

            engine.resume();
            check(!engine.isFadingOut(),
                   "pressing Play during a fade-out cancels it rather than continuing down");

            engine.fadeOutAndStop(2.0);
            engine.pause();
            check(!engine.isFadingOut(),
                   "Pause during a fade-out abandons it, so resuming isn't mysteriously quiet");

            engine.hardStop();
            engine.fadeOutAndStop(2.0);
            check(!engine.isFadingOut(),
                   "Fade out does nothing when there is nothing playing");

            // Crossfade off
            engine.setCrossfadeEnabled(false);
            check(!engine.isCrossfadeEnabled(), "crossfade can be switched off");

            engine.resume();
            auto before = engine.getCurrentTrackFile();
            engine.skipToNext();
            check(!engine.isCrossfading(),
                   "with crossfade off a skip cuts straight over instead of starting a fade");
            check(engine.getCurrentTrackFile() != before,
                   "the cut still actually moves to the next track");
            check(engine.isPlaying(), "and it is still playing afterwards");

            engine.setCrossfadeEnabled(true);
            engine.setCrossfadeSeconds(500.0);
            check(engine.getCrossfadeSeconds() == PlaylistEngine::kMaxCrossfadeSeconds,
                   "an absurd crossfade length is clamped to the usable range");
            engine.setCrossfadeSeconds(4.0);
            check(engine.getCrossfadeSeconds() == 4.0, "a sensible crossfade length is kept");

            engine.skipToNext();
            check(engine.isCrossfading(), "with crossfade back on, a skip fades again");

            engine.hardStop();
        }

        {
            // Per-track volume trims.
            TrackSettingsStore gains;
            gains.setFile(scratch.getChildFile("gains.json"));
            gains.load();

            auto loud = folderTracks[0];
            auto quiet = folderTracks[1];

            check(gains.getGainDb(loud) == 0.0f && gains.getLinearGain(loud) == 1.0f,
                   "a track with no trim set plays at its own level");

            gains.setGainDb(loud, -6.0f);
            check(gains.getGainDb(loud) == -6.0f, "a trim is remembered");
            check(gains.getLinearGain(loud) < 1.0f, "a negative trim actually turns the track down");
            check(gains.getNumEntries() == 1, "only trimmed tracks take up space");

            gains.setGainDb(loud, -200.0f);
            check(gains.getGainDb(loud) == TrackSettingsStore::kMinDb,
                   "a trim beyond the usable range is clamped, not stored as-is");

            gains.setGainDb(loud, 0.0f);
            check(gains.getNumEntries() == 0,
                   "setting a track back to normal removes the entry rather than storing a no-op");

            gains.setGainDb(loud, -3.5f);
            gains.setGainDb(quiet, 2.0f);

            {
                TrackSettingsStore reloaded;
                reloaded.setFile(scratch.getChildFile("gains.json"));
                reloaded.load();
                check(reloaded.getGainDb(loud) == -3.5f && reloaded.getGainDb(quiet) == 2.0f,
                       "trims survive a round-trip to disk");

                // Windows paths are case-insensitive; the same track
                // reached two ways must not end up with two trims.
                juce::File shouted(loud.getFullPathName().toUpperCase());
                check(reloaded.getGainDb(shouted) == -3.5f,
                       "a trim is found regardless of how the path was cased");
            }

            {
                auto future = scratch.getChildFile("future-gains.json");
                future.replaceWithText("{ \"schemaVersion\": 99 }");
                auto before = future.loadFileAsString();

                TrackSettingsStore newer;
                newer.setFile(future);
                newer.load();
                check(!newer.getLoadWarnings().isEmpty(), "a newer trims file is reported");
                check(future.loadFileAsString() == before, "a newer trims file is left untouched");

                auto broken = scratch.getChildFile("broken-gains.json");
                broken.replaceWithText("{ not json");
                TrackSettingsStore bad;
                bad.setFile(broken);
                bad.load();
                check(!bad.getLoadWarnings().isEmpty(), "an unreadable trims file is reported, not fatal");
            }

            // The trim has to actually reach the deck, not just the file.
            PlaylistEngine trimmed(formatManager);
            trimmed.setShuffle(false);
            trimmed.setTrackGainProvider([&gains](const juce::File& f) { return gains.getLinearGain(f); });
            trimmed.setTracks(folderTracks);
            trimmed.start();

            check(juce::approximatelyEqual(trimmed.getCurrentTrackGain(),
                                            juce::Decibels::decibelsToGain(-3.5f)),
                   "a track's trim is applied to the deck when it starts playing");

            gains.setGainDb(loud, -12.0f);
            trimmed.refreshTrackGains();
            check(juce::approximatelyEqual(trimmed.getCurrentTrackGain(),
                                            juce::Decibels::decibelsToGain(-12.0f)),
                   "changing a trim while that track plays takes effect immediately");
            trimmed.stop();

            // ---- per-track fade lengths ----------------------------------
            check(gains.getFadeSeconds(loud) == 0.0,
                   "a track with no fade set follows the global crossfade length");
            check(!gains.hasFade(loud), "and doesn't claim to have one of its own");

            gains.setFadeSeconds(loud, 8.0);
            check(gains.getFadeSeconds(loud) == 8.0, "a track's fade length is remembered");
            check(gains.getGainDb(loud) == -12.0f,
                   "setting a fade doesn't disturb the trim on the same track");

            gains.setFadeSeconds(loud, 900.0);
            check(gains.getFadeSeconds(loud) == TrackSettingsStore::kMaxFadeSeconds,
                   "an absurd fade length is clamped");

            gains.setFadeSeconds(loud, 8.0);
            gains.setGainDb(loud, 0.0f);
            check(gains.hasFade(loud),
                   "clearing the trim leaves the fade alone - they are separate settings");
            check(gains.getNumEntries() == 2, "a track with only a fade is still stored");

            gains.setFadeSeconds(loud, 0.0);
            check(gains.getNumEntries() == 1,
                   "a track back to normal on BOTH settings drops out of the file entirely");

            {
                // The engine asks the OUTGOING track how long to take.
                PlaylistEngine fades(formatManager);
                fades.setShuffle(false);
                fades.setCrossfadeSeconds(3.0);
                fades.setTrackFadeProvider([&gains](const juce::File& f) { return gains.getFadeSeconds(f); });
                fades.setTracks(folderTracks);

                gains.setFadeSeconds(folderTracks[0], 9.0);
                fades.resume();
                check(fades.getCurrentTrackFile() == folderTracks[0], "playing the track with a custom fade");

                fades.skipToNext();
                check(fades.isCrossfading(), "a skip from it starts a crossfade");
                check(fades.getActiveCrossfadeSeconds() == 9.0,
                       "the fade uses the LEAVING track's own length, not the global default");

                fades.hardStop();

                // A track with no fade of its own falls back to the global.
                fades.resume();
                fades.skipToNext();
                check(fades.getActiveCrossfadeSeconds() == 9.0,
                       "still the custom length while that track is the one leaving");

                gains.setFadeSeconds(folderTracks[0], 0.0);
                fades.hardStop();
                fades.resume();
                fades.skipToNext();
                check(fades.getActiveCrossfadeSeconds() == 3.0,
                       "clearing a track's fade puts it back on the global length");

                gains.setFadeSeconds(folderTracks[0], 0.0);
            }

            {
                // The file this replaced must still be readable, or trims
                // set in beta.7 vanish on upgrade.
                auto legacy = scratch.getChildFile("legacy-gains.json");
                auto current = scratch.getChildFile("legacy-migration").getChildFile("track-settings.json");
                current.getParentDirectory().createDirectory();

                auto legacyInPlace = current.getSiblingFile("track-gains.json");
                legacyInPlace.replaceWithText("{ \"schemaVersion\": 1, \"gains\": { \"c:\\\\music\\\\hot.wav\": -7.5 } }");
                juce::ignoreUnused(legacy);

                TrackSettingsStore upgraded;
                upgraded.setFile(current);
                upgraded.load();

                check(upgraded.getGainDb(juce::File("C:\\music\\hot.wav")) == -7.5f,
                       "trims from the previous version's file are read on upgrade");
                check(current.existsAsFile() == false,
                       "reading the old file doesn't write the new one until something changes");
            }
        }

        {
            // Per-button soundboard volume and background pictures.
            auto boardFile2 = scratch.getChildFile("board-extras.json");
            SoundboardLayout extras(formatManager);
            extras.setFile(boardFile2);
            extras.load();
            extras.assign(0, folderTracks[0]);

            check(extras.getSlot(0).gainDb == 0.0f && extras.getLinearGain(0) == 1.0f,
                   "a new soundboard button starts at its sound's own level");

            extras.setGainDb(0, -8.0f);
            check(extras.getLinearGain(0) < 1.0f, "a button's trim turns its sound down");
            extras.setGainDb(0, 99.0f);
            check(extras.getSlot(0).gainDb == SoundboardLayout::kMaxGainDb,
                   "a button's trim is clamped to the usable range");
            extras.setGainDb(0, -8.0f);

            check(inkwyrd::isImageFile(juce::File("C:/x/art.PNG")), "a picture is recognised by extension");
            check(!inkwyrd::isImageFile(folderTracks[0]), "an audio file is not treated as a picture");

            auto picture = scratch.getChildFile("button.png");
            picture.replaceWithText("not really a png, but the layout only checks the extension");
            check(extras.setImage(0, picture), "a picture can be set on a button");
            check(!extras.setImage(0, folderTracks[0]), "a non-picture is refused as a background");
            check(extras.getSlot(0).imageFile == picture, "the refused one didn't replace the good one");

            {
                SoundboardLayout reloaded(formatManager);
                reloaded.setFile(boardFile2);
                reloaded.load();
                check(reloaded.getSlot(0).gainDb == -8.0f, "a button's trim survives a round-trip");
                check(reloaded.getSlot(0).imageFile == picture, "a button's picture survives a round-trip");

                reloaded.clearImage(0);
                check(reloaded.getSlot(0).imageFile == juce::File(), "a picture can be removed");
                check(reloaded.getSlot(0).gainDb == -8.0f,
                       "removing the picture leaves the trim alone");
            }

            // Trims and pictures are why the schema went to 2: an older
            // build must refuse the file rather than rewrite it without
            // them.
            check(SoundboardLayout::kCurrentSchemaVersion == 2,
                   "the soundboard schema version was bumped for the new fields");
            check(boardFile2.loadFileAsString().contains("\"schemaVersion\": 2"),
                   "the new schema version is what actually gets written");
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

        {
            // REAL playback, with the timer running and audio actually
            // being pulled.
            //
            // Every other check in this file drives the engine by calling
            // its methods and inspecting state. None of them ever let a
            // crossfade RUN TO COMPLETION, because that needs
            // juce::Timer to fire, which needs a message loop. beta.8
            // shipped a bug that only appears at that exact moment - the
            // deck that had just become active was handed a gain of zero,
            // so playback went silent the instant any crossfade finished
            // - and 155 passing checks said nothing about it.
            auto makeTone = [&](const juce::File& file, double frequency, int seconds)
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
                if (stream == nullptr)
                    return false;

                std::unique_ptr<juce::AudioFormatWriter> writer(
                    wav.createWriterFor(stream.get(), 44100.0, 1, 16, {}, 0));
                if (writer == nullptr)
                    return false;

                stream.release(); // the writer owns it now

                juce::AudioBuffer<float> block(1, 44100);
                for (int second = 0; second < seconds; ++second)
                {
                    for (int i = 0; i < 44100; ++i)
                        block.setSample(0, i, 0.35f * std::sin(2.0 * juce::MathConstants<double>::pi
                                                                * frequency * i / 44100.0));
                    writer->writeFromAudioSampleBuffer(block, 0, 44100);
                }

                return true;
            };

            auto toneA = scratch.getChildFile("tone-a.wav");
            auto toneB = scratch.getChildFile("tone-b.wav");
            auto tonesOk = makeTone(toneA, 220.0, 8) && makeTone(toneB, 330.0, 8);
            check(tonesOk, "test tones were written (everything below depends on this)");

            if (tonesOk)
            {
                juce::Array<juce::File> tones;
                tones.add(toneA);
                tones.add(toneB);

                PlaylistEngine engine(formatManager);
                engine.setShuffle(false);
                engine.setCrossfadeSeconds(0.5); // keep the test quick
                engine.setTracks(tones);
                engine.prepareToPlay(512, 44100.0);
                engine.resume();

                // The audio thread's job, at roughly real-time pace -
                // without something consuming blocks the transport never
                // advances and no transition would ever be reached.
                std::atomic<bool> pulling { true };
                std::atomic<float> magnitude { 0.0f };

                // Locked to the WALL CLOCK, not to sleep_for. Windows'
                // default timer granularity is ~15.6 ms, so sleeping 11 ms
                // per 512-sample block plays audio at about three-quarters
                // speed - which silently turns every timing assertion
                // below into a measurement of the wrong thing. This pulls
                // however many blocks real time says are owed.
                std::thread puller([&]
                {
                    juce::AudioBuffer<float> buffer(2, 512);
                    auto startTime = std::chrono::steady_clock::now();
                    juce::int64 samplesPulled = 0;

                    while (pulling.load())
                    {
                        auto elapsed = std::chrono::duration<double>(
                                            std::chrono::steady_clock::now() - startTime).count();
                        auto owed = (juce::int64) (elapsed * 44100.0);

                        while (samplesPulled < owed && pulling.load())
                        {
                            buffer.clear();
                            juce::AudioSourceChannelInfo info(&buffer, 0, 512);
                            engine.getNextAudioBlock(info);
                            magnitude.store(buffer.getMagnitude(0, 512));
                            samplesPulled += 512;
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                });

                juce::MessageManager::getInstance()->runDispatchLoopUntil(700);
                check(magnitude.load() > 0.01f, "audio actually comes out when playback starts");

                engine.skipToNext();
                check(engine.isCrossfading(), "the skip started a crossfade");

                juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);
                check(!engine.isCrossfading(), "the crossfade finished on its own");
                check(engine.getCurrentTrackFile() == toneB, "and left the next track playing");
                check(magnitude.load() > 0.01f,
                       "audio is STILL coming out after a completed crossfade "
                       "(beta.8 shipped silence here)");

                // And it keeps going, rather than being silenced a moment later.
                juce::MessageManager::getInstance()->runDispatchLoopUntil(700);
                check(magnitude.load() > 0.01f, "and keeps playing afterwards");

                engine.pause();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
                check(magnitude.load() < 0.01f, "Pause really does silence the output");

                engine.resume();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
                check(magnitude.load() > 0.01f, "and Play brings it back");

                pulling.store(false);
                puller.join();

                engine.hardStop();
                engine.releaseResources();
            }

            // Looping a single track. A SHORT tone, so a whole loop cycle
            // - play out, hold the gap, start again - happens inside the
            // test rather than eight seconds later. The entire feature is
            // "what happens when a track reaches its end", which no
            // amount of poking at state can observe.
            auto shortTone = scratch.getChildFile("tone-short.wav");
            auto shortOk = makeTone(shortTone, 220.0, 2);
            check(shortOk, "a short test tone was written");

            if (shortOk)
            {
                juce::Array<juce::File> oneTone;
                oneTone.add(shortTone);

                PlaylistEngine engine(formatManager);
                engine.setShuffle(false);
                engine.setCrossfadeEnabled(false); // straight cut, so the gap is the only silence
                engine.setTracks(oneTone);
                engine.prepareToPlay(512, 44100.0);

                check(!engine.isLoopEnabled(), "looping is off unless asked for");
                engine.setLoopEnabled(true);
                engine.setLoopGapSeconds(99.0);
                check(engine.getLoopGapSeconds() == PlaylistEngine::kMaxLoopGapSeconds,
                       "an absurd loop gap is clamped");
                engine.setLoopGapSeconds(1.0);

                std::atomic<bool> pulling { true };
                std::atomic<float> magnitude { 0.0f };

                // Locked to the WALL CLOCK, not to sleep_for. Windows'
                // default timer granularity is ~15.6 ms, so sleeping 11 ms
                // per 512-sample block plays audio at about three-quarters
                // speed - which silently turns every timing assertion
                // below into a measurement of the wrong thing. This pulls
                // however many blocks real time says are owed.
                std::thread puller([&]
                {
                    juce::AudioBuffer<float> buffer(2, 512);
                    auto startTime = std::chrono::steady_clock::now();
                    juce::int64 samplesPulled = 0;

                    while (pulling.load())
                    {
                        auto elapsed = std::chrono::duration<double>(
                                            std::chrono::steady_clock::now() - startTime).count();
                        auto owed = (juce::int64) (elapsed * 44100.0);

                        while (samplesPulled < owed && pulling.load())
                        {
                            buffer.clear();
                            juce::AudioSourceChannelInfo info(&buffer, 0, 512);
                            engine.getNextAudioBlock(info);
                            magnitude.store(buffer.getMagnitude(0, 512));
                            samplesPulled += 512;
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                });

                engine.resume();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(600);
                check(magnitude.load() > 0.01f, "the looping track starts playing");

                // Past the end of a 2 s tone, so it should now be sitting
                // in the 1 s of silence between repeats.
                juce::MessageManager::getInstance()->runDispatchLoopUntil(1900);
                check(engine.isWaitingForLoopGap(), "the track holds the gap after it plays out");
                check(magnitude.load() < 0.01f, "and the gap really is silent");
                check(engine.getCurrentTrackFile() == shortTone,
                       "the track it will come back to is still the same one");

                // ...and then comes back on its own.
                juce::MessageManager::getInstance()->runDispatchLoopUntil(1200);
                check(!engine.isWaitingForLoopGap(), "the gap ends by itself");
                check(magnitude.load() > 0.01f, "and the track starts again - it LOOPED");

                // Play out of a gap should restart immediately rather than
                // waiting the remainder out.
                juce::MessageManager::getInstance()->runDispatchLoopUntil(1900);
                if (engine.isWaitingForLoopGap())
                {
                    engine.resume();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
                    check(!engine.isWaitingForLoopGap() && magnitude.load() > 0.01f,
                           "Play during the gap starts the track again straight away");
                }

                engine.pause();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
                check(!engine.isWaitingForLoopGap(),
                       "Pause doesn't leave a gap counting down with nothing to end it");
                check(magnitude.load() < 0.01f, "and Pause silences a looping track");

                engine.resume();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
                check(magnitude.load() > 0.01f, "Play brings the loop back");

                engine.setLoopEnabled(false);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
                check(magnitude.load() > 0.01f, "switching looping off doesn't stop what's playing");

                pulling.store(false);
                puller.join();

                engine.hardStop();
                engine.releaseResources();
            }
        }

        scratch.deleteRecursively();

        std::cout << (failures == 0 ? "SELF-TEST PASSED" : "SELF-TEST FAILED")
                   << " (" << failures << " failure(s))" << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // INKWYRD_ICONRENDER=<folder>: renders the app icon as a PNG at every
    // size a Windows .ico carries, from the same drawLogo() the title bars
    // use - so the icon is the in-app mark rather than a second drawing of
    // it that could drift. installer/make-icon.ps1 packs the PNGs into
    // installer/InkwyrdAudio.ico; icon-256.png is also the ICON_BIG JUCE
    // builds into the exe. Re-run both when the logo changes.
    int runIconRender(const juce::File& folder)
    {
        using namespace inkwyrd::theme;

        juce::ScopedJuceInitialiser_GUI gui; // fonts, for the "W"
        folder.createDirectory();

        for (int size : { 16, 20, 24, 32, 40, 48, 64, 128, 256 })
        {
            juce::Image image(juce::Image::ARGB, size, size, true);

            {
                juce::Graphics g(image);

                // Designed on a 24-unit grid - the size the logo was drawn
                // for - and scaled, so its fixed-width strokes stay in
                // proportion at 256px instead of turning into hairlines.
                g.addTransform(juce::AffineTransform::scale((float) size / 24.0f));

                juce::Rectangle<float> tile(0.5f, 0.5f, 23.0f, 23.0f);
                g.setColour(panelDeep);
                g.fillRoundedRectangle(tile, 5.0f);
                g.setColour(outline);
                g.drawRoundedRectangle(tile, 5.0f, 1.0f);

                InkwyrdLookAndFeel::drawLogo(g, tile.reduced(2.5f), accent, accentSoft.withAlpha(0.45f));
            }

            auto file = folder.getChildFile("icon-" + juce::String(size) + ".png");
            file.deleteFile();

            juce::FileOutputStream out(file);
            if (! out.openedOk() || ! juce::PNGImageFormat().writeImageToStream(image, out))
            {
                std::cout << "Couldn't write " << file.getFullPathName() << std::endl;
                return 1;
            }

            std::cout << file.getFullPathName() << std::endl;
        }

        return 0;
    }

    // INKWYRD_TAGTEST=1: embedded-tag reading, against whatever is
    // actually in the user's track library rather than a synthetic
    // fixture.
    //
    // A fixture would prove the parser runs; it would not prove the
    // thing that was actually in doubt, which is whether the app can get
    // tags out of the formats real libraries are made of. This one
    // reports coverage per format and fails only if it can read NOTHING
    // - a library of genuinely untagged files is a legitimate result.
    int runTagTest()
    {
        TrackLibrary library;
        library.setFile(TrackLibrary::getDefaultFile());
        library.load();

        auto tracks = library.getAllTracks();
        std::cout << "  library holds " << tracks.size() << " track(s)" << std::endl;

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        formats.registerFormat(new Mp3AudioFormat(), false);
        formats.registerFormat(new MediaFoundationAudioFormat(), false);

#if JUCE_WINDOWS
        // The property-store fallback needs COM on the calling thread; the
        // app does this on its scan thread, so a probe calling
        // readFromFile() directly has to do it here.
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

        std::map<juce::String, std::pair<int, int>> byExtension; // ext -> {tagged, total}
        int examined = 0;

        for (const auto& file : tracks)
        {
            if (! file.existsAsFile())
                continue;

            auto metadata = TrackMetadataStore::readFromFile(file, formats);
            auto extension = file.getFileExtension().toLowerCase();

            auto& counts = byExtension[extension];
            counts.second++;
            if (metadata.title.isNotEmpty() || metadata.artist.isNotEmpty())
                counts.first++;

            if (examined < 3)
            {
                std::cout << "    " << file.getFileName() << std::endl
                           << "      title=\"" << metadata.title << "\""
                           << " artist=\"" << metadata.artist << "\""
                           << " album=\"" << metadata.album << "\""
                           << " genre=\"" << metadata.genre << "\""
                           << " year=" << metadata.year
                           << " track=" << metadata.trackNumber << std::endl;
            }

            ++examined;
        }

        int totalTagged = 0;
        for (const auto& pair : byExtension)
        {
            std::cout << "  " << pair.first << ": " << pair.second.first
                       << "/" << pair.second.second << " tagged" << std::endl;
            totalTagged += pair.second.first;
        }

        check(examined > 0, "found readable files in the track library to examine");
        check(totalTagged > 0, "read embedded tags from at least one real file");

        // The specific thing this exists to catch: MP3 goes through
        // dr_mp3, which knows nothing about ID3, so if the property-store
        // fallback ever stops working this is where it shows up.
        auto mp3 = byExtension.find(".mp3");
        if (mp3 != byExtension.end() && mp3->second.second > 0)
            check(mp3->second.first > 0,
                   "read tags from MP3s, which JUCE's reader cannot do on its own");

        std::cout << (failures == 0 ? "TAG-TEST PASSED" : "TAG-TEST FAILED")
                   << " (" << failures << " failure(s))" << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // INKWYRD_RPCTEST=1: the one piece of the Discord auto-mute path that
    // can be checked without Discord running.
    //
    // Deriving the application id from the bot token is the step where a
    // silent wrong answer is most costly: a plausible-looking but
    // incorrect id produces a handshake that Discord simply refuses,
    // which reads as "authorisation failed" and sends you looking at
    // scopes and secrets instead. Everything else in DiscordRpcClient
    // needs a live client and a human clicking a consent dialog, and is
    // covered by scratchpad/rpc_mute_spike.py instead.
    //
    // No real token appears here or is read from anywhere - these are
    // constructed from a known id.
    int runRpcTest()
    {
        auto tokenFor = [](const juce::String& applicationId)
        {
            juce::MemoryOutputStream encoded;
            juce::Base64::convertToBase64(encoded, applicationId.toRawUTF8(),
                                           (size_t) applicationId.getNumBytesAsUTF8());

            // Discord uses base64url and drops the padding.
            auto segment = encoded.toString().replaceCharacter('+', '-')
                                              .replaceCharacter('/', '_')
                                              .removeCharacters("=");
            return segment + ".Gabcde.abcdefghijklmnopqrstuvwxyz12";
        };

        check(DiscordRpcClient::deriveApplicationId(tokenFor("1543399962723745792"))
                  == "1543399962723745792",
               "application id round-trips out of a bot token");

        check(DiscordRpcClient::deriveApplicationId(tokenFor("123456789012345678"))
                  == "123456789012345678",
               "a different application id derives correctly too");

        // Everything below must fail rather than return something
        // plausible - a wrong id fails later, further from the cause.
        check(DiscordRpcClient::deriveApplicationId("").isEmpty(),
               "an empty token derives nothing");
        check(DiscordRpcClient::deriveApplicationId("not-a-token").isEmpty(),
               "a token-shaped-but-not string derives nothing");
        check(DiscordRpcClient::deriveApplicationId("aGVsbG8.x.y").isEmpty(),
               "a token whose first segment decodes to non-digits derives nothing");
        check(DiscordRpcClient::deriveApplicationId("MTIz.x.y").isEmpty(),
               "a decoded value too short to be a snowflake derives nothing");

        // Whitespace round a pasted token is extremely common and must
        // not change the answer.
        check(DiscordRpcClient::deriveApplicationId("  " + tokenFor("1543399962723745792") + "  ")
                  == "1543399962723745792",
               "leading/trailing whitespace on a pasted token is tolerated");

        std::cout << (failures == 0 ? "RPC-TEST PASSED" : "RPC-TEST FAILED")
                   << " (" << failures << " failure(s))" << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // INKWYRD_NOISETEST=1: the mic noise suppressor, exercised without an
    // audio device. Needs no files and no PLAYLIST_FOLDER - it
    // synthesises its own signal, so it runs anywhere.
    //
    // What this is really guarding is the PLUMBING, not RNNoise: the
    // library's own behaviour was measured separately in src/mic-spike.
    // What can break here is the bridging - 480-sample frames against
    // arbitrary device blocks, and 48k against a device that isn't at
    // 48k. Both are easy to get subtly wrong in ways that still produce
    // plausible-sounding audio, so they get real assertions: exact
    // sample counts out, no underruns in steady state, and actual
    // attenuation of a noise-only signal.
    int runNoiseSuppressorTest()
    {
        // A noise-only signal is the cleanest thing to assert on: with no
        // voice present, a working suppressor should drive it towards
        // silence, and any figure near 0dB means it isn't running.
        auto fillWithNoise = [](juce::AudioBuffer<float>& buffer, juce::Random& rng)
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample(ch, i, 0.05f * (rng.nextFloat() * 2.0f - 1.0f));
        };

        auto runAtRate = [&](double sampleRate, int blockSize, const juce::String& label)
        {
            NoiseSuppressor suppressor;
            suppressor.prepare(sampleRate, blockSize);
            suppressor.setEnabled(true);

            juce::Random rng(1234);
            juce::AudioBuffer<float> block(2, blockSize);

            double inputEnergy = 0.0, outputEnergy = 0.0;
            int measuredSamples = 0;

            // 3 seconds. The first second is discarded: the pipeline
            // primes with silence by design, and counting that as
            // "attenuation" would be measuring the wrong thing entirely.
            auto totalBlocks = (int) (3.0 * sampleRate / blockSize);
            auto skipBlocks = (int) (1.0 * sampleRate / blockSize);

            for (int b = 0; b < totalBlocks; ++b)
            {
                fillWithNoise(block, rng);

                double blockInput = 0.0;
                for (int i = 0; i < blockSize; ++i)
                    blockInput += (double) block.getSample(0, i) * block.getSample(0, i);

                suppressor.process(block, blockSize);

                if (b < skipBlocks)
                    continue;

                double blockOutput = 0.0;
                for (int i = 0; i < blockSize; ++i)
                    blockOutput += (double) block.getSample(0, i) * block.getSample(0, i);

                inputEnergy += blockInput;
                outputEnergy += blockOutput;
                measuredSamples += blockSize;

                // Both channels must carry the same suppressed signal -
                // see NoiseSuppressor.h on why this is mono by design.
                check(block.getSample(0, blockSize / 2) == block.getSample(1, blockSize / 2),
                       label + ": both channels carry the same suppressed signal");
            }

            auto reductionDb = 10.0 * std::log10((outputEnergy + 1e-20) / (inputEnergy + 1e-20));
            std::cout << "  " << label << ": noise-only change " << juce::String(reductionDb, 1)
                       << " dB, " << suppressor.getUnderrunCount() << " underrun(s)" << std::endl;

            check(reductionDb < -20.0,
                   label + ": noise-only signal is attenuated by more than 20dB");
            check(suppressor.getUnderrunCount() == 0,
                   label + ": no output underruns in steady state");
            check(measuredSamples > 0, label + ": actually measured something");
        };

        // The common case, and the one where no resampling happens at all.
        runAtRate(48000.0, 480, "48k / 480-sample blocks");

        // A block size unrelated to RNNoise's 480 - the frame FIFO has to
        // carry a partial frame across block boundaries.
        runAtRate(48000.0, 512, "48k / 512-sample blocks");

        // The case that needs resampling in both directions. 44.1k is
        // common enough on Windows that treating it as exotic would be a
        // mistake, and the interpolators are where drift would show up.
        runAtRate(44100.0, 441, "44.1k / 441-sample blocks");
        runAtRate(44100.0, 1024, "44.1k / 1024-sample blocks");

        // Disabled must be a true no-op, not a quiet passthrough with
        // latency: someone who never turns this on must not pay for it.
        {
            NoiseSuppressor suppressor;
            suppressor.prepare(48000.0, 480);
            suppressor.setEnabled(false);

            juce::AudioBuffer<float> block(2, 480);
            juce::Random rng(99);
            fillWithNoise(block, rng);

            juce::AudioBuffer<float> before(block);
            suppressor.process(block, 480);

            bool identical = true;
            for (int i = 0; i < 480 && identical; ++i)
                identical = block.getSample(0, i) == before.getSample(0, i);

            check(identical, "disabled: the buffer is left completely untouched");
            check(suppressor.getLatencySamples() == 0, "disabled: reports no added latency");
            check(suppressor.getLatencyMs() > 0.0,
                   "disabled: still reports what enabling it would cost");
        }

        std::cout << (failures == 0 ? "NOISE-TEST PASSED" : "NOISE-TEST FAILED")
                   << " (" << failures << " failure(s))" << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // INKWYRD_SNAPTEST=1: the magnetic-snap geometry behind the
    // Winamp-style multi-window layout, exercised without launching the
    // app or dragging anything - snapRectangle() takes no Component/peer,
    // so this is plain input/output checking.
    int runSnapTest()
    {
        using juce::Rectangle;
        constexpr int threshold = 10;

        // Screen-edge snap: candidate's right edge is 8px short of the
        // screen's right edge (within threshold) - it should snap flush.
        {
            Rectangle<int> screen(0, 0, 1920, 1080);
            Rectangle<int> candidate(1612, 100, 300, 200); // right = 1912
            auto result = inkwyrd::snapRectangle(candidate, {}, screen, threshold);
            check(result.getRight() == screen.getRight(), "snaps flush to the screen's right edge");
            check(result.getY() == candidate.getY(), "vertical position untouched by a horizontal-only snap");
        }

        // Single obstacle: candidate docks to the right of it (its left
        // edge lines up with the obstacle's right edge).
        {
            Rectangle<int> screen(0, 0, 4000, 4000); // far enough away to never interfere
            Rectangle<int> obstacle(100, 100, 300, 200); // right edge = 400
            Rectangle<int> candidate(408, 300, 250, 150); // left = 408, 8px short of 400
            auto result = inkwyrd::snapRectangle(candidate, { obstacle }, screen, threshold);
            check(result.getX() == obstacle.getRight(), "docks flush to the right of a single obstacle");
        }

        // No snap when far: nothing within threshold on either axis -
        // the rectangle comes back completely unchanged.
        {
            Rectangle<int> screen(0, 0, 4000, 4000);
            Rectangle<int> obstacle(100, 100, 300, 200);
            Rectangle<int> candidate(1000, 1000, 300, 200);
            auto result = inkwyrd::snapRectangle(candidate, { obstacle }, screen, threshold);
            check(result == candidate, "far from everything, the rectangle is returned unchanged");
        }

        // Multiple obstacles: only the nearby one should affect the
        // result - a distant obstacle must not be mistaken for the close
        // one. (Not named near/far: both are legacy macros in the
        // Windows SDK headers JUCE pulls in on this platform.)
        {
            Rectangle<int> screen(0, 0, 4000, 4000);
            Rectangle<int> distantObstacle(0, 0, 50, 50);
            Rectangle<int> closeObstacle(500, 500, 200, 100); // bottom edge = 600
            Rectangle<int> candidate(550, 608, 150, 150); // top = 608, 8px short of 600
            auto result = inkwyrd::snapRectangle(candidate, { distantObstacle, closeObstacle }, screen, threshold);
            check(result.getY() == closeObstacle.getBottom(), "snaps to the nearby obstacle's bottom edge, not the distant one");
            check(result.getX() == candidate.getX(), "horizontal position untouched - no x-axis target was close");
        }

        // ---- resizing: only the dragged edge moves ----
        {
            Rectangle<int> screen(0, 0, 4000, 4000);
            Rectangle<int> neighbour(500, 100, 300, 200); // left edge 500

            // Dragging the RIGHT edge towards the neighbour's left edge.
            Rectangle<int> candidate(100, 100, 394, 200); // right = 494, 6px short
            auto result = inkwyrd::snapResizedEdges(candidate, { neighbour }, screen, threshold,
                                                     false, true, false, false);
            check(result.getRight() == neighbour.getX(), "a dragged right edge snaps flush to a neighbour");
            check(result.getX() == candidate.getX(),
                   "and the opposite edge does NOT move - it resized, it didn't slide");
            check(result.getY() == candidate.getY() && result.getBottom() == candidate.getBottom(),
                   "the untouched axis is left alone entirely");
        }

        {
            // An edge that ISN'T being dragged must never snap, even
            // when it happens to sit right next to something.
            Rectangle<int> screen(0, 0, 4000, 4000);
            Rectangle<int> neighbour(500, 100, 300, 200);
            Rectangle<int> candidate(494, 100, 300, 200); // LEFT edge 6px from neighbour
            auto result = inkwyrd::snapResizedEdges(candidate, { neighbour }, screen, threshold,
                                                     false, true, false, false); // dragging RIGHT only
            check(result.getX() == candidate.getX(), "an edge that isn't being dragged is never snapped");
        }

        {
            // A corner drag snaps both of its edges.
            Rectangle<int> screen(0, 0, 4000, 4000);
            Rectangle<int> neighbour(500, 500, 300, 200);
            Rectangle<int> candidate(100, 100, 394, 394); // right 494, bottom 494
            auto result = inkwyrd::snapResizedEdges(candidate, { neighbour }, screen, threshold,
                                                     false, true, false, true);
            check(result.getRight() == neighbour.getX() && result.getBottom() == neighbour.getY(),
                   "dragging a corner snaps both of its edges");
        }

        {
            // A snap that would invert the window is refused outright.
            Rectangle<int> screen(0, 0, 4000, 4000);
            Rectangle<int> neighbour(0, 0, 10, 10);
            Rectangle<int> candidate(5, 100, 12, 200); // right edge 17, near neighbour's 10
            auto result = inkwyrd::snapResizedEdges(candidate, { neighbour }, screen, threshold,
                                                     false, true, false, false);
            check(result.getWidth() > 0 && result.getHeight() > 0,
                   "a snap that would turn the window inside out is refused, not applied");
        }

        // ---- docking, i.e. what a drag should carry along with it ----
        constexpr int dockTolerance = 4;

        // Side by side and flush: docked.
        {
            Rectangle<int> a(0, 0, 100, 100);
            Rectangle<int> b(100, 0, 100, 100); // a's right edge == b's left
            check(inkwyrd::areRectanglesDocked(a, b, dockTolerance), "flush side-by-side windows are docked");
            check(inkwyrd::areRectanglesDocked(b, a, dockTolerance), "and docking is symmetric");
        }

        // Stacked and flush: docked.
        {
            Rectangle<int> a(0, 0, 100, 100);
            Rectangle<int> b(0, 100, 100, 100); // a's bottom == b's top
            check(inkwyrd::areRectanglesDocked(a, b, dockTolerance), "flush stacked windows are docked");
        }

        // A couple of pixels out, within tolerance: still docked, since a
        // hand-placed window is rarely exactly flush.
        {
            Rectangle<int> a(0, 0, 100, 100);
            Rectangle<int> b(102, 0, 100, 100);
            check(inkwyrd::areRectanglesDocked(a, b, dockTolerance), "a 2px gap is still docked");
        }

        // Clearly apart: not docked.
        {
            Rectangle<int> a(0, 0, 100, 100);
            Rectangle<int> b(400, 0, 100, 100);
            check(! inkwyrd::areRectanglesDocked(a, b, dockTolerance), "windows far apart are not docked");
        }

        // THE important negative case: edges align numerically but the
        // windows share no actual edge - only a corner. Dragging one must
        // not drag the other.
        {
            Rectangle<int> a(0, 0, 100, 100);
            Rectangle<int> b(100, 100, 100, 100); // touches a only at one point
            check(! inkwyrd::areRectanglesDocked(a, b, dockTolerance),
                   "corner-to-corner windows are NOT docked - they share a point, not an edge");
        }

        // Aligned on one axis but nowhere near on the other.
        {
            Rectangle<int> a(0, 0, 100, 100);
            Rectangle<int> b(100, 500, 100, 100); // x flush, y miles away
            check(! inkwyrd::areRectanglesDocked(a, b, dockTolerance),
                   "a flush edge with no overlap on the other axis is not docked");
        }

        // Transitive chain: dragging A should carry B (touching A) AND C
        // (touching only B), but not D sitting on its own.
        {
            juce::Array<Rectangle<int>> chain;
            chain.add({ 0, 0, 100, 100 });      // 0: A, the dragged one
            chain.add({ 100, 0, 100, 100 });    // 1: B, flush against A
            chain.add({ 200, 0, 100, 100 });    // 2: C, flush against B only
            chain.add({ 900, 900, 100, 100 });  // 3: D, unattached
            auto group = inkwyrd::findDockedGroup(chain, 0, dockTolerance);
            check(group.size() == 2, "a docked chain is followed transitively (A picks up B and C)");
            check(group.contains(1) && group.contains(2), "and it's the right two");
            check(! group.contains(3), "an unattached window is left behind");
            check(! group.contains(0), "the dragged window isn't listed as its own follower");
        }

        // Nothing attached: empty group, and no crash on a lone window.
        {
            juce::Array<Rectangle<int>> lonely;
            lonely.add({ 0, 0, 100, 100 });
            check(inkwyrd::findDockedGroup(lonely, 0, dockTolerance).isEmpty(),
                   "a window with no neighbours carries nothing");
            check(inkwyrd::findDockedGroup(lonely, 7, dockTolerance).isEmpty(),
                   "an out-of-range index is handled rather than read off the end");
        }

        std::cout << (failures == 0 ? "SNAP-TEST PASSED" : "SNAP-TEST FAILED")
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

    // Pure geometry, no files/folders/audio device involved - checked
    // before the PLAYLIST_FOLDER requirement below applies to it.
    if (juce::SystemStats::getEnvironmentVariable("INKWYRD_SNAPTEST", "").isNotEmpty())
        return runSnapTest();

    // Also self-contained - synthesises its own audio, needs no device
    // and no music folder, so it goes above the PLAYLIST_FOLDER guard
    // for the same reason the snap test does.
    if (juce::SystemStats::getEnvironmentVariable("INKWYRD_NOISETEST", "").isNotEmpty())
        return runNoiseSuppressorTest();

    if (juce::SystemStats::getEnvironmentVariable("INKWYRD_RPCTEST", "").isNotEmpty())
        return runRpcTest();

    // Reads the user's REAL library, so it needs no fixture folder and
    // goes above the PLAYLIST_FOLDER guard like the others.
    if (juce::SystemStats::getEnvironmentVariable("INKWYRD_TAGTEST", "").isNotEmpty())
        return runTagTest();

    auto iconFolder = juce::SystemStats::getEnvironmentVariable("INKWYRD_ICONRENDER", "");
    if (iconFolder.isNotEmpty())
        return runIconRender(juce::File(iconFolder));

    auto playlistFolder = juce::SystemStats::getEnvironmentVariable("PLAYLIST_FOLDER", "");
    auto soundboardFolder = juce::SystemStats::getEnvironmentVariable("SOUNDBOARD_FOLDER", "");

    if (playlistFolder.isEmpty())
    {
        std::cout << "Set PLAYLIST_FOLDER (and optionally SOUNDBOARD_FOLDER) env vars first." << std::endl;
        return 1;
    }

    // INKWYRD_RENDERTEST=<out.png> paints the soundboard grid and the
    // playlist panel offscreen and saves the result.
    //
    // Component::createComponentSnapshot() runs the real paint code
    // without a desktop window, so the new per-button volume bars and
    // background pictures can be checked without launching the app -
    // which matters because the app shares %APPDATA% with the user's live
    // session, and seeding a fixture there while they are using it has
    // already cost one accidental clobbering of their settings.
    {
        auto renderTo = juce::SystemStats::getEnvironmentVariable("INKWYRD_RENDERTEST", "");
        if (renderTo.isNotEmpty())
        {
            juce::ScopedJuceInitialiser_GUI juceInitialiser;
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();

            auto scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("inkwyrd-render-test");
            scratch.deleteRecursively();
            scratch.createDirectory();

            auto musicFolder = juce::File(playlistFolder);
            auto tracks = inkwyrd::scanFolderForAudio(musicFolder, fm, true);
            if (tracks.size() < 3)
            {
                std::cout << "RENDER TEST needs at least 3 audio files in PLAYLIST_FOLDER" << std::endl;
                return 1;
            }

            // A real picture, so the button actually has something to draw.
            juce::Image art(juce::Image::RGB, 160, 100, false);
            {
                juce::Graphics g(art);
                g.setGradientFill(juce::ColourGradient(juce::Colours::darkorange, 0.0f, 0.0f,
                                                        juce::Colours::darkblue, 160.0f, 100.0f, false));
                g.fillAll();
            }
            auto artFile = scratch.getChildFile("art.png");
            {
                juce::FileOutputStream out(artFile);
                juce::PNGImageFormat png;
                png.writeImageToStream(art, out);
            }

            TrackSettingsStore gains;
            gains.setFile(scratch.getChildFile("gains.json"));
            gains.setGainDb(tracks[0], -12.0f);
            gains.setGainDb(tracks[1], 4.0f);
            gains.setFadeSeconds(tracks[2], 8.0);
            gains.setFadeSeconds(tracks[0], 1.5);

            SoundboardLayout board(fm);
            board.setFile(scratch.getChildFile("board.json"));
            board.load();
            board.assign(0, tracks[0], "Door creak");
            board.setGainDb(0, -9.0f);
            board.setImage(0, artFile);
            board.assign(1, tracks[1], "Thunder");
            board.assign(2, tracks[2], "Sword clash");
            board.setGainDb(2, 4.5f);
            board.setColour(2, 0xff8c2f2f);

            SoundboardEngine sfx(fm);
            SoundboardGridComponent grid(sfx, board, [] {});
            grid.setSize(700, 400);

            PlaylistLibrary library(fm);
            library.setDirectory(scratch.getChildFile("playlists"));
            library.loadAll();
            auto& list = library.createPlaylist("Ambient");
            library.addFolderLink(list.id, musicFolder, true);

            PlaylistEngine engine(fm);
            TrackLibrary renderTracks;
            renderTracks.setFile(scratch.getChildFile("render-track-library.json"));
            renderTracks.registerTracks(library.scanFolder(musicFolder, true));

            TrackMetadataStore renderMetadata;
            PlaylistPanel panel(library, renderTracks, engine, gains, renderMetadata,
                                 [](const juce::Uuid&) {}, [](const juce::Uuid&) {},
                                 [](const juce::Uuid&) {});
            panel.setSize(440, 400);

            // The popup a volume bar expands into. Rendered here because
            // opening it for real needs a click, and synthetic clicks in
            // this app have proven unreliable (see CLAUDE.md).
            VolumeCallout callout("Some Long Track Name", -6.0f,
                                   TrackSettingsStore::kMinDb, TrackSettingsStore::kMaxDb,
                                   [](float) {});
            callout.addFadeControl(8.0, TrackSettingsStore::kMaxFadeSeconds, [](double) {});

            // Side by side, the way they appear in the app, with the
            // popup underneath.
            juce::Image sheet(juce::Image::RGB, 440 + 700 + 24, 400 + 16 + callout.getHeight(), true);
            {
                juce::Graphics g(sheet);
                g.fillAll(juce::Colour(0xff2b3540));
                g.drawImageAt(panel.createComponentSnapshot(panel.getLocalBounds()), 0, 0);
                g.drawImageAt(grid.createComponentSnapshot(grid.getLocalBounds()), 464, 0);
                g.drawImageAt(callout.createComponentSnapshot(callout.getLocalBounds()), 0, 416);
            }

            juce::File outFile(renderTo);
            outFile.deleteFile();
            juce::FileOutputStream out(outFile);
            juce::PNGImageFormat png;
            auto ok = png.writeImageToStream(sheet, out);

            std::cout << (ok ? "RENDER TEST wrote " : "RENDER TEST FAILED writing ")
                       << outFile.getFullPathName().toStdString() << std::endl;
            return ok ? 0 : 1;
        }
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
