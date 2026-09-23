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
#include "TrackSearch.h"
#include "DuckEnvelope.h"
#include "SceneLibrary.h"
#include "ScenesComponent.h"
#include "SceneEditor.h"
#include "UpdateCheck.h"
#include "SkinLoader.h"
#include "SkinSpriteNames.h"
#include "TagEditor.h"
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
            // beta.28.1: the Setup music folder's tracks reach All Tracks.
            // First run used to make the playlist but never put its tracks
            // in the library - so a new user had music playing and an EMPTY
            // All Tracks, the only place to preview, search or tag from.
            // Everything in a folder of its own: the playlist tests below
            // read every .json at the top of `scratch` as a playlist.
            auto setupScratch = scratch.getChildFile("setup-folder-test");
            setupScratch.createDirectory();

            PlaylistLibrary setupLibrary(formatManager);
            setupLibrary.setDirectory(setupScratch.getChildFile("playlists"));
            setupLibrary.loadAll();

            auto& fromSetup = setupLibrary.createFromLegacyFolder(musicFolder);
            check(setupLibrary.tracksLinkedToFolder(musicFolder).size() == folderTracks.size(),
                   "every track from the Setup music folder's playlist is found for the library");

            // Only what came through THAT folder - a file added to the
            // same playlist by hand is somebody else's decision.
            auto elsewhere = setupScratch.getChildFile("elsewhere.wav");
            folderTracks[0].copyFileTo(elsewhere);
            setupLibrary.addFiles(fromSetup.id, { elsewhere });
            check(! setupLibrary.tracksLinkedToFolder(musicFolder).contains(elsewhere),
                   "a file added to that playlist by hand is not counted as the folder's");

            check(setupLibrary.tracksLinkedToFolder(setupScratch.getChildFile("no-such-folder")).isEmpty(),
                   "a folder no playlist links to gives nothing");

            TrackLibrary library;
            library.setFile(setupScratch.getChildFile("track-library.json"));
            library.load();
            library.registerTracks(setupLibrary.tracksLinkedToFolder(musicFolder));
            check(library.getNumTracks() == folderTracks.size(),
                   "so All Tracks has the Setup folder's music in it");

            setupScratch.deleteRecursively();
        }

        {
            // Removing a SELECTION from a playlist (beta.28.2). The trap is
            // order: every removal shifts the indices after it, so taking
            // entries out lowest-first would remove the wrong tracks.
            auto removeScratch = scratch.getChildFile("remove-entries-test");
            removeScratch.createDirectory();

            // Six distinct files, whatever the fixture folder holds.
            juce::Array<juce::File> six;
            for (int i = 0; i < 6; ++i)
            {
                auto source = folderTracks[i % folderTracks.size()];
                auto copy = removeScratch.getChildFile("track-" + juce::String(i) + source.getFileExtension());
                source.copyFileTo(copy);
                six.add(copy);
            }

            PlaylistLibrary removeLibrary(formatManager);
            removeLibrary.setDirectory(removeScratch.getChildFile("playlists"));
            removeLibrary.loadAll();

            auto& list = removeLibrary.createPlaylist("Session");
            auto id = list.id;
            removeLibrary.addFiles(id, six);

            auto entryPaths = [&removeLibrary, id]
            {
                juce::StringArray names;
                for (const auto& entry : removeLibrary.findById(id)->entries)
                    names.add(entry.path.getFileNameWithoutExtension());
                return names;
            };

            check(entryPaths().size() == 6, "a playlist of six tracks to remove from");

            // Out of order, with a repeat and an index past the end - all
            // things a selection handed over as-is can contain.
            removeLibrary.removeEntries(id, { 4, 1, 1, 99, 2 });
            check(entryPaths() == juce::StringArray { "track-0", "track-3", "track-5" },
                   "removing several tracks takes out exactly those - not the ones their positions shift onto");

            PlaylistLibrary reloaded(formatManager);
            reloaded.setDirectory(removeScratch.getChildFile("playlists"));
            reloaded.loadAll();
            auto* back = reloaded.findById(id);
            check(back != nullptr && back->entries.size() == 3,
                   "and the smaller playlist is what's saved");

            removeLibrary.removeEntries(id, { 0, 1, 2 });
            check(entryPaths().isEmpty(), "every track can be removed at once (Ctrl+A, then Delete)");

            removeLibrary.removeEntries(id, {});
            removeLibrary.removeEntries(juce::Uuid(), { 0 });
            check(entryPaths().isEmpty(), "an empty selection, or a playlist that doesn't exist, does nothing");

            for (const auto& file : six)
                check(file.existsAsFile(), "removing a track from a playlist never deletes the file: "
                                            + file.getFileName());

            {
                // Reordering (beta.30). A playlist's order IS its entry
                // order, so moving tracks up and down the Playlist window
                // is this - and getting the arithmetic wrong here silently
                // rearranges somebody's session.
                PlaylistLibrary orderLibrary(formatManager);
                orderLibrary.setDirectory(removeScratch.getChildFile("order-playlists"));
                orderLibrary.loadAll();

                auto& ordered = orderLibrary.createPlaylist("Order");
                auto orderId = ordered.id;
                orderLibrary.addFiles(orderId, six);

                auto names = [&orderLibrary, orderId]
                {
                    juce::StringArray out;
                    for (const auto& entry : orderLibrary.findById(orderId)->entries)
                        out.add(entry.path.getFileNameWithoutExtension().fromLastOccurrenceOf("track-", false, false));
                    return out.joinIntoString(",");
                };

                check(names() == "0,1,2,3,4,5", "six tracks in the order they were added");

                // One track down, then back up: the pair of moves the
                // Down and Up keys make.
                check(orderLibrary.moveEntriesBy(orderId, { 2 }, 1), "a track moves down");
                check(names() == "0,1,3,2,4,5", "and lands one place later");
                check(orderLibrary.moveEntriesBy(orderId, { 3 }, -1), "and back up");
                check(names() == "0,1,2,3,4,5", "to exactly where it was");

                // A block keeps its own order and stays together.
                check(orderLibrary.moveEntriesBy(orderId, { 0, 1 }, 1), "a block of tracks moves down together");
                check(names() == "2,0,1,3,4,5", "keeping their order among themselves");

                // The ends.
                check(! orderLibrary.moveEntriesBy(orderId, { 0 }, -1), "the top track can't go up");
                check(! orderLibrary.moveEntriesBy(orderId, { 5 }, 1), "the bottom track can't go down");
                check(names() == "2,0,1,3,4,5", "and neither attempt changes anything");

                // Dropping a dragged selection: before the entry at that
                // index, whether it comes from above or below.
                check(orderLibrary.moveEntriesTo(orderId, { 4 }, 0), "a track can be dragged to the top");
                check(names() == "4,2,0,1,3,5", "landing before everything else");
                check(orderLibrary.moveEntriesTo(orderId, { 0 }, 6), "and dragged to the very end");
                check(names() == "2,0,1,3,5,4", "landing after everything else");

                check(! orderLibrary.moveEntriesTo(orderId, { 2 }, 2),
                       "dropping a track exactly where it already is does nothing");
                check(! orderLibrary.moveEntriesTo(orderId, { 99 }, 0), "an index that isn't there does nothing");

                PlaylistLibrary reloadedOrder(formatManager);
                reloadedOrder.setDirectory(removeScratch.getChildFile("order-playlists"));
                reloadedOrder.loadAll();
                juce::StringArray saved;
                for (const auto& entry : reloadedOrder.findById(orderId)->entries)
                    saved.add(entry.path.getFileNameWithoutExtension().fromLastOccurrenceOf("track-", false, false));
                check(saved.joinIntoString(",") == "2,0,1,3,5,4", "and the new order is what's saved");
            }

            removeScratch.deleteRecursively();
        }

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
            // Skins. These rules are what a skin author actually hits -
            // a typo, a missing key, a file from a newer version - so
            // they are checked here rather than by opening Settings.
            using inkwyrd::SkinLoader;

            juce::Colour colour;
            check(SkinLoader::parseColour("#4fe08a", colour) && colour == juce::Colour(0xff4fe08a),
                   "a #rrggbb colour parses, opaque");
            check(SkinLoader::parseColour("4fe08a", colour) && colour == juce::Colour(0xff4fe08a),
                   "the hash is optional");
            check(SkinLoader::parseColour("804fe08a", colour) && colour.getAlpha() == 0x80,
                   "eight digits carry their own alpha");
            check(! SkinLoader::parseColour("#4fe08", colour), "a five-digit colour is refused, not guessed at");
            check(! SkinLoader::parseColour("green", colour), "a colour name is refused");
            check(! SkinLoader::parseColour("", colour), "an empty colour is refused");

            check(! SkinLoader::parse(juce::var("nonsense"), {}).ok,
                   "a skin file that isn't a JSON object is refused with a message");

            auto parseText = [](const char* text)
            {
                juce::var parsed;
                juce::JSON::parse(juce::String(text), parsed);
                return SkinLoader::parse(parsed, {});
            };

            auto newer = parseText(R"({ "schemaVersion": 99, "colours": { "accent": "#ffffff" } })");
            check(! newer.ok && newer.message.contains("newer"),
                   "a skin from a newer version is skipped rather than half-read");

            auto partial = parseText(R"({ "name": "Partial", "colours": { "accent": "#ff0000" } })");
            auto builtIn = inkwyrd::theme::builtIn();
            check(partial.ok, "a skin that sets one colour still loads");
            check(partial.palette.accent == juce::Colour(0xffff0000), "and that colour is used");
            check(partial.palette.background == builtIn.background,
                   "while everything it left out keeps the built-in value");
            check(partial.name == "Partial", "the skin's own name is used when it has one");

            auto odd = parseText(R"({ "colours": { "accnet": "#ff0000", "accent": "lime" } })");
            check(odd.ok, "a typo doesn't stop the rest of the skin loading");
            check(odd.warnings.size() == 2, "but both the unknown key and the bad value are reported");
            check(odd.palette.accent == builtIn.accent, "and an unreadable colour keeps the built-in one");

            auto sized = parseText(R"({ "metrics": { "titleBarHeight": 2, "cornerRadius": 3.5 } })");
            check(sized.palette.titleBarHeight == inkwyrd::theme::Palette::kMinTitleBarHeight,
                   "a title bar too small to drag is clamped");
            check(! sized.warnings.isEmpty(), "and the author is told it was clamped");
            check(juce::approximatelyEqual(sized.palette.cornerRadius, 3.5f), "corner radius comes through");

            auto fonts = parseText(R"({ "fonts": { "digits": "Courier New", "wrong": "x" } })");
            check(fonts.palette.digitFontName == "Courier New", "a font family is taken from the skin");
            check(fonts.palette.titleFontName == builtIn.titleFontName, "and the others are left alone");
            check(fonts.warnings.size() == 1, "an unknown font slot is reported");

            auto missingLogo = parseText(R"({ "logo": "nope.png" })");
            check(missingLogo.ok && missingLogo.logoFile == juce::File(),
                   "a logo that isn't there leaves the drawn mark");
            check(! missingLogo.warnings.isEmpty(), "and says so");

            // Export -> read back: what the Settings button writes has to
            // be a skin the app can load, or it's a useless template.
            auto custom = builtIn;
            custom.accent = juce::Colour(0xff112233);
            custom.textDim = juce::Colour(0x80aabbcc);
            custom.titleBarHeight = 52;
            custom.cornerRadius = 2.0f;
            custom.digitFontName = "Courier New";

            auto skinFolder = scratch.getChildFile("skins").getChildFile("Exported");
            juce::String writeError;
            check(SkinLoader::writeToFolder(custom, "Exported", skinFolder, writeError),
                   "the current look can be exported as a skin");

            auto reloaded = SkinLoader::loadFromFolder(skinFolder);
            check(reloaded.ok, "and read straight back in");
            check(reloaded.palette.accent == custom.accent
                   && reloaded.palette.textDim == custom.textDim
                   && reloaded.palette.titleBarHeight == custom.titleBarHeight
                   && reloaded.palette.digitFontName == custom.digitFontName,
                   "with every value intact, alpha included");

            auto folders = SkinLoader::findSkinFolders(scratch.getChildFile("skins"));
            check(folders.size() == 1 && folders[0].getFileName() == "Exported",
                   "and it is listed as an available skin");

            auto corrupt = scratch.getChildFile("skins").getChildFile("Broken");
            corrupt.createDirectory();
            corrupt.getChildFile("skin.json").replaceWithText("{ this is not json");
            auto broken = SkinLoader::loadFromFolder(corrupt);
            check(! broken.ok && broken.message.isNotEmpty(),
                   "a corrupt skin reports an error rather than crashing");
            check(SkinLoader::findSkinFolders(scratch.getChildFile("skins")).size() == 2,
                   "a broken skin is still listed, so its author can see it");
        }

        {
            // Skin sprites. Same idea as the palette checks above: these
            // are the mistakes a skin author makes (a rect off the sheet,
            // a typo'd name, a slice wider than its sprite), and the
            // drawing rules that make nine-slice pixel art hold together.
            using inkwyrd::SkinLoader;
            using inkwyrd::SkinSprites;
            using inkwyrd::SpriteState;

            // A 32 x 16 sheet. The left 16 x 16 is a "button": a 4px red
            // frame round a blue middle. The right half is solid green.
            juce::Image sheet(juce::Image::ARGB, 32, 16, true);
            {
                juce::Graphics g(sheet);
                g.setColour(juce::Colours::red);
                g.fillRect(0, 0, 16, 16);
                g.setColour(juce::Colours::blue);
                g.fillRect(4, 4, 8, 8);
                g.setColour(juce::Colours::lime);
                g.fillRect(16, 0, 16, 16);
            }

            auto parseSprites = [&](const char* text, const juce::Image& sheet2x, juce::StringArray& warnings)
            {
                juce::var parsed;
                juce::JSON::parse(juce::String(text), parsed);
                return SkinSprites::parseWithImages(parsed, sheet, sheet2x, warnings);
            };

            juce::StringArray warnings;
            auto good = parseSprites(R"({ "items": {
                    "button":      { "rect": [0, 0, 16, 16], "slice": [4, 4, 4, 4] },
                    "button@down": { "rect": [16, 0, 16, 16] },
                    "button.transport.play": { "rect": [16, 0, 8, 8] },
                    "icon.toggle.shuffle":   { "rect": [16, 0, 4, 4] } } })", {}, warnings);

            check(good.size() == 4 && warnings.isEmpty(), "a well-formed sprite section loads every item, quietly");
            check(good.find("button") != nullptr && good.find("button")->image1x.getWidth() == 16,
                   "each sprite is cut out of the sheet at its own size");
            check(good.find("button")->slice.getLeft() == 4 && good.find("button")->slice.getBottom() == 4,
                   "and keeps its nine-slice insets");

            // State fallbacks.
            bool exact = true;
            check(good.findForState("button", SpriteState::over, {}, &exact) == good.find("button") && ! exact,
                   "a state with no art of its own falls back to the plain sprite, and says it did");
            check(good.findForState("button", SpriteState::down, {}, &exact) == good.find("button@down") && exact,
                   "a state that has art uses it");
            check(good.findForState("button", SpriteState::onDown) == good.find("button@down"),
                   "an engaged button being pressed falls back to the pressed art");
            check(good.findForState("button", SpriteState::down, "transport.play") == good.find("button.transport.play"),
                   "a control's OWN sprite wins over the generic pressed one");
            check(good.findForState("button", SpriteState::normal, "transport.stop") == good.find("button"),
                   "a control with no sprite of its own uses the generic one");
            check(good.findForState("icon", SpriteState::normal, "toggle.loop") == nullptr,
                   "an icon is never borrowed from another control");

            // The author's mistakes.
            warnings.clear();
            auto bad = parseSprites(R"({ "scale": 20, "items": {
                    "buton":                 { "rect": [0, 0, 4, 4] },
                    "button.transport.plya": { "rect": [0, 0, 4, 4] },
                    "icon":                  { "rect": [0, 0, 4, 4] },
                    "panel":                 { "rect": [30, 0, 8, 8] },
                    "well":                  { "rect": [0, 0, 8, 8], "slice": [5, 0, 5, 0] },
                    "popup":                 { "rect": [0, 0, 2.5, 8] },
                    "slider.thumb.master@down": { "rect": [0, 0, 4, 4] } } })", {}, warnings);

            check(bad.find("buton") == nullptr && bad.find("button.transport.plya") == nullptr
                   && bad.find("icon") == nullptr,
                   "misspelt names - including a misspelt control - are ignored");
            check(bad.find("panel") == nullptr, "a sprite reaching off the sheet is ignored");
            check(bad.find("popup") == nullptr, "a sprite at a fractional position is refused, not rounded");
            check(bad.find("well") != nullptr && bad.find("well")->slice.getLeft() == 0,
                   "a slice wider than its sprite is dropped, and the sprite stretched whole");
            check(bad.find("slider.thumb.master@down") != nullptr, "a control's own state variant is recognised");
            check(juce::approximatelyEqual(bad.getScale(), SkinSprites::kMaxScale), "a silly scale is clamped");
            check(warnings.size() == 7, "and every one of those mistakes is reported by name");

            warnings.clear();
            juce::Image wrong2x(juce::Image::ARGB, 60, 32, true);
            auto mismatched = parseSprites(R"({ "items": { "button": { "rect": [0, 0, 16, 16] } } })", wrong2x, warnings);
            check(! mismatched.hasHiResSheet() && warnings.size() == 1,
                   "a 2x sheet that isn't exactly twice the size is ignored, with a warning");

            warnings.clear();
            auto right2x = sheet.rescaled(64, 32, juce::Graphics::lowResamplingQuality);
            auto hiRes = parseSprites(R"({ "items": { "button": { "rect": [0, 0, 16, 16] } } })", right2x, warnings);
            check(hiRes.hasHiResSheet() && hiRes.find("button")->image2x.getWidth() == 32,
                   "a correct 2x sheet is used, sprite for sprite");

            // Drawing. Nine-slice into 40 x 20 at scale 1: corners keep
            // their real 4px, the middle stretches, nothing is smoothed.
            {
                juce::Image target(juce::Image::ARGB, 40, 20, true);
                {
                    // Scoped: on Windows a juce::Image is a Direct2D bitmap,
                    // and nothing reaches its pixels until the Graphics is
                    // gone.
                    juce::Graphics g(target);
                    good.drawNineSlice(g, *good.find("button"), { 0.0f, 0.0f, 40.0f, 20.0f });
                }

                check(target.getPixelAt(0, 0) == juce::Colours::red && target.getPixelAt(39, 19) == juce::Colours::red,
                       "nine-slice keeps the corners");
                check(target.getPixelAt(3, 10) == juce::Colours::red && target.getPixelAt(36, 10) == juce::Colours::red,
                       "the frame stays 4px wide however wide the control gets");
                check(target.getPixelAt(4, 4) == juce::Colours::blue && target.getPixelAt(20, 10) == juce::Colours::blue
                       && target.getPixelAt(35, 15) == juce::Colours::blue,
                       "and the middle stretches to fill, with no blending at its edges");
            }

            {
                // Smaller than its own corners: they shrink in proportion,
                // so both ends still show rather than one covering the other.
                juce::Image target(juce::Image::ARGB, 6, 6, true);
                {
                    juce::Graphics g(target);
                    good.drawNineSlice(g, *good.find("button"), { 0.0f, 0.0f, 6.0f, 6.0f });
                }
                check(target.getPixelAt(0, 0) == juce::Colours::red && target.getPixelAt(5, 5) == juce::Colours::red,
                       "a control smaller than the sprite's corners still gets both ends");
            }

            {
                // Scale 2: every sheet pixel becomes a 2 x 2 block.
                warnings.clear();
                auto doubled = parseSprites(R"({ "scale": 2, "items": {
                        "button": { "rect": [0, 0, 16, 16], "slice": [4, 4, 4, 4] } } })", {}, warnings);
                juce::Image target(juce::Image::ARGB, 40, 40, true);
                {
                    juce::Graphics g(target);
                    doubled.drawNineSlice(g, *doubled.find("button"), { 0.0f, 0.0f, 40.0f, 40.0f });
                }
                check(target.getPixelAt(7, 20) == juce::Colours::red && target.getPixelAt(8, 20) == juce::Colours::blue,
                       "at scale 2 a 4px frame is drawn 8px wide");
            }

            {
                // Tiling: a 2-colour stripe repeats along the edge rather
                // than being smeared across it.
                // 6 x 3, 2px corners each side; the 2px middle is one
                // white column then one black.
                juce::Image stripes(juce::Image::ARGB, 6, 3, true);
                {
                    juce::Graphics sg(stripes);
                    sg.fillAll(juce::Colours::black);
                    sg.setColour(juce::Colours::white);
                    sg.fillRect(2, 0, 1, 3);
                }

                warnings.clear();
                juce::var parsed;
                juce::JSON::parse(R"({ "items": { "titlebar": { "rect": [0, 0, 6, 3], "slice": [2, 0, 2, 0], "tile": true } } })",
                                   parsed);
                auto tiled = SkinSprites::parseWithImages(parsed, stripes, {}, warnings);

                juce::Image target(juce::Image::ARGB, 14, 3, true);
                {
                    juce::Graphics g(target);
                    tiled.drawNineSlice(g, *tiled.find("titlebar"), { 0.0f, 0.0f, 14.0f, 3.0f });
                }
                check(target.getPixelAt(2, 1) == juce::Colours::white && target.getPixelAt(3, 1) == juce::Colours::black
                       && target.getPixelAt(4, 1) == juce::Colours::white && target.getPixelAt(5, 1) == juce::Colours::black,
                       "a tiled edge repeats its pattern instead of stretching it");
            }

            // Through SkinLoader, from real files - the path the app takes.
            {
                auto folder = scratch.getChildFile("skins").getChildFile("Sprited");
                folder.createDirectory();

                juce::PNGImageFormat png;
                {
                    juce::FileOutputStream out(folder.getChildFile("sprites.png"));
                    png.writeImageToStream(sheet, out);
                }

                folder.getChildFile("skin.json").replaceWithText(R"({
                    "name": "Sprited",
                    "colours": { "accent": "#ff0000" },
                    "sprites": { "sheet": "sprites.png", "scale": 2,
                                 "items": { "button": { "rect": [0, 0, 16, 16], "slice": [4, 4, 4, 4] } } } })");

                auto loaded = SkinLoader::loadFromFolder(folder);
                check(loaded.ok && loaded.sprites.size() == 1 && loaded.warnings.isEmpty(),
                       "a skin folder with a sprite sheet loads its sprites");
                check(loaded.palette.accent == juce::Colour(0xffff0000),
                       "alongside its colours");

                folder.getChildFile("sprites.png").deleteFile();
                auto sheetless = SkinLoader::loadFromFolder(folder);
                check(sheetless.ok && sheetless.sprites.isEmpty() && ! sheetless.warnings.isEmpty(),
                       "a missing sheet still loads the skin's colours, and says the sprites are off");

                auto noSprites = SkinLoader::loadFromFolder(scratch.getChildFile("skins").getChildFile("Exported"));
                check(noSprites.ok && noSprites.sprites.isEmpty() && noSprites.warnings.isEmpty(),
                       "a skin with no sprites section is exactly what it was before sprites existed");
            }

            // Every name the app asks for must be accepted, or a skin
            // could never supply it.
            bool allKnown = true;
            for (auto* base : inkwyrd::sprites::kBaseNames)
                for (auto suffix : { "", "@over", "@down", "@on", "@disabled" })
                    allKnown = allKnown && SkinSprites::isKnownName(juce::String(base) + suffix);
            for (auto* id : inkwyrd::sprites::kComponentIds)
                allKnown = allKnown && SkinSprites::isKnownName("button." + juce::String(id))
                                    && SkinSprites::isKnownName("icon." + juce::String(id) + "@down");
            check(allKnown, "every sprite name the app paints with is one a skin can supply");
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
            // The Library search box's matching rule. Worth testing
            // headlessly because it decides what a user can FIND - a
            // filter that silently drops a track looks exactly like a
            // library that lost it.
            using inkwyrd::matchesSearchTerms;

            // What the panel builds: tags plus the filename, so untagged
            // tracks are still findable by what they're called on disk.
            const juce::String row = "Blue Drake Troyificus Sentinel Ambient 03 blue-drake-final";

            check(matchesSearchTerms(row, {}), "an empty search matches everything");
            check(matchesSearchTerms(row, "   "), "a search of only spaces matches everything");
            check(matchesSearchTerms(row, "blue"), "a title word matches");
            check(matchesSearchTerms(row, "BLUE"), "matching ignores case");
            check(matchesSearchTerms(row, "troyificus"), "an artist matches");
            check(matchesSearchTerms(row, "sentinel"), "an album matches");
            check(matchesSearchTerms(row, "ambient"), "a genre matches");
            check(matchesSearchTerms(row, "final"), "the filename matches when the tags do not");
            check(matchesSearchTerms(row, "drake blue"),
                   "words match in any order, not as one run of text");
            check(matchesSearchTerms(row, "blue   drake"), "repeated spaces between words are ignored");
            check(matchesSearchTerms(row, "blue sentinel"),
                   "words from different fields match together");
            check(! matchesSearchTerms(row, "blue tavern"),
                   "every word has to match - one hit is not enough");
            check(! matchesSearchTerms(row, "tavern"), "a track that matches nothing is filtered out");
            check(matchesSearchTerms(row, "rak"), "a partial word matches, so typing narrows as you go");
        }

        {
            // Ducking the music under the mic. All of the behaviour worth
            // checking is in the envelope, and none of it needs an audio
            // device: made-up mic levels, real time passing in blocks.
            constexpr double sampleRate = 48000.0;
            constexpr int blockSamples = 480; // 10 ms, so a block count IS a time

            auto loud = juce::Decibels::decibelsToGain(-6.0f);
            auto quiet = juce::Decibels::decibelsToGain(-60.0f);

            auto runBlocks = [&](inkwyrd::DuckEnvelope& envelope, float micPeak, int blocks)
            {
                float gain = envelope.getCurrentGain();
                for (int i = 0; i < blocks; ++i)
                    gain = envelope.processBlock(micPeak, blockSamples);

                return gain;
            };

            inkwyrd::DuckSettings settings;
            settings.enabled = true;
            settings.amountDb = -12.0f;
            settings.thresholdDb = -40.0f;

            auto duckedGain = juce::Decibels::decibelsToGain(settings.amountDb);

            {
                // Off: a loud mic must not move the music at all. This is
                // the check that matters most - the feature is opt-in,
                // and it must be genuinely inert until someone asks.
                inkwyrd::DuckEnvelope envelope;
                envelope.prepare(sampleRate);

                check(juce::approximatelyEqual(runBlocks(envelope, loud, 50), 1.0f),
                       "ducking switched off leaves the music at full level");
            }

            {
                inkwyrd::DuckEnvelope envelope;
                envelope.prepare(sampleRate);
                envelope.setSettings(settings);

                check(juce::approximatelyEqual(runBlocks(envelope, quiet, 50), 1.0f),
                       "a mic below the threshold never ducks");

                // 150 ms of speech against a 30 ms attack. The move is
                // exponential, so "reached" means five time constants,
                // not one: at 100 ms it is still 3% short, which is
                // correct behaviour rather than a slow attack.
                auto afterAttack = runBlocks(envelope, loud, 15);
                check(std::abs(afterAttack - duckedGain) < 0.02f,
                       "speech pulls the music down to the set amount");

                // The hold is what stops the music surging between words.
                auto duringPause = runBlocks(envelope, quiet, 20); // 200 ms < 400 ms hold
                check(std::abs(duringPause - duckedGain) < 0.02f,
                       "the music stays down through a pause between words");

                // Hold then release, with enough time for the same
                // exponential to actually arrive: 400 ms of hold plus a
                // 600 ms time constant means a few seconds, not one.
                auto afterRelease = runBlocks(envelope, quiet, 800);
                check(juce::approximatelyEqual(afterRelease, 1.0f),
                       "the music comes all the way back when speaking stops");
            }

            {
                // The gain has to stay inside its own range the whole way
                // down: a duck that overshoots is a dip, which is worse
                // than no ducking at all.
                inkwyrd::DuckEnvelope envelope;
                envelope.prepare(sampleRate);
                envelope.setSettings(settings);

                auto withinRange = true;
                for (int i = 0; i < 100; ++i)
                {
                    auto gain = envelope.processBlock(loud, blockSamples);
                    withinRange = withinRange && gain <= 1.0f && gain >= duckedGain - 0.001f;
                }

                check(withinRange, "the ducked gain never overshoots past the set amount");
            }

            {
                // Switching it off mid-duck has to let go, not leave the
                // music quietly pinned down for the rest of the session.
                inkwyrd::DuckEnvelope envelope;
                envelope.prepare(sampleRate);
                envelope.setSettings(settings);
                runBlocks(envelope, loud, 20);

                auto off = settings;
                off.enabled = false;
                envelope.setSettings(off);

                check(juce::approximatelyEqual(runBlocks(envelope, loud, 800), 1.0f),
                       "turning ducking off releases the music even with the mic still live");
            }
        }

        {
            // Which release is newer. Worth testing because the whole
            // point is a notice that only appears when it should: a
            // wrong answer either nags about an update that doesn't
            // exist, or silently never mentions one that does.
            using inkwyrd::isNewerRelease;

            check(isNewerRelease("0.1.0-beta.23", "0.1.0-beta.24"), "a later beta is newer");
            check(! isNewerRelease("0.1.0-beta.24", "0.1.0-beta.23"), "an earlier beta is not newer");
            check(! isNewerRelease("0.1.0-beta.24", "0.1.0-beta.24"), "the same version is not newer");
            check(isNewerRelease("0.1.0-beta.9", "0.1.0-beta.23"),
                   "betas compare as numbers, not text - beta.23 beats beta.9");
            check(isNewerRelease("0.1.0-beta.22", "0.1.0-beta.22.1"), "a hotfix on a beta is newer");
            check(! isNewerRelease("0.1.0-beta.22.1", "0.1.0-beta.22"), "and the beta it fixed is not");
            check(isNewerRelease("0.1.0-beta.24", "0.1.0"), "a finished release beats its own betas");
            check(! isNewerRelease("0.1.0", "0.1.0-beta.24"), "and a beta of it does not beat the release");
            check(isNewerRelease("0.1.0-beta.24", "0.2.0-beta.1"), "a later base version wins outright");
            check(isNewerRelease("v0.1.0-beta.23", "v0.1.0-beta.24"), "a leading v on either side is ignored");
            check(! isNewerRelease("0.1.0-beta.24", "nonsense"),
                   "an unparseable tag is never treated as an update");

            // What GitHub actually sends back, rather than a shape we
            // hope it sends.
            auto parsed = inkwyrd::parseReleasesJson(
                R"([{"tag_name":"v0.1.0-beta.24","draft":false,)"
                R"("html_url":"https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.24"}])");

            check(parsed.valid && parsed.version == "0.1.0-beta.24",
                   "the newest release's version is read from the releases JSON");
            check(parsed.url.contains("releases/tag"), "so is the link to it");

            // Rate-limit replies and error bodies are objects, not
            // arrays, and must simply say nothing.
            check(! inkwyrd::parseReleasesJson(R"({"message":"API rate limit exceeded"})").valid,
                   "a rate-limit reply is not mistaken for a release");
            check(! inkwyrd::parseReleasesJson("<html>captive portal</html>").valid,
                   "a page that isn't JSON at all is not mistaken for a release");
            check(! inkwyrd::parseReleasesJson("[]").valid, "no releases at all is not a release");
            check(! inkwyrd::parseReleasesJson(R"([{"tag_name":"v9.9.9","draft":true}])").valid,
                   "a draft release is skipped - nobody else can download it");

            // The update LINK: whatever the reply says, it may only open
            // this project's own GitHub pages.
            using inkwyrd::safeReleasePageUrl;
            const juce::String releases = inkwyrd::kReleasesPageUrl;
            const juce::String real = "https://github.com/Troyificus/InkWyrd-Audio/releases/tag/v0.1.0-beta.29";

            check(safeReleasePageUrl(real) == real, "a real release page is linked to as it is");
            check(safeReleasePageUrl("https://example.com/download") == releases,
                   "another site becomes the Releases page instead");
            check(safeReleasePageUrl("https://github.com.evil.example/Troyificus/InkWyrd-Audio/x") == releases,
                   "a lookalike host is refused");
            check(safeReleasePageUrl("https://github.com/Troyificus/InkWyrd-Audio-evil/releases") == releases,
                   "a lookalike repository name is refused");
            check(safeReleasePageUrl("http://github.com/Troyificus/InkWyrd-Audio/releases/tag/v1") == releases,
                   "plain http is refused");
            check(safeReleasePageUrl("https://github.com/Troyificus/InkWyrd-Audio/../../someone/else") == releases,
                   "a path that climbs out of the repository is refused");
            check(safeReleasePageUrl({}) == releases, "no address at all gives the Releases page");
        }

        {
            // What pressing a scene should CHANGE. Every rule that makes
            // scenes feel right is in planScene, so this is where they are
            // proven - with no window, no audio device and no timing.
            juce::Uuid tavernList, combatList, deletedList;

            SceneContext context;
            context.playlistExists = [&](const juce::Uuid& id) { return id == tavernList || id == combatList; };
            context.boardNames = juce::StringArray { "Rain", "Fire", "Crowd", "Wind", "Clang" };
            context.loopingNames = juce::StringArray { "Rain", "Fire", "Crowd", "Wind" };

            Scene combat;
            combat.name = "Combat";
            combat.music = Scene::Music::playPlaylist;
            combat.playlistId = combatList;
            combat.loops = juce::StringArray { "Rain", "Wind" };

            // From a tavern with rain and a fire going.
            context.activePlaylistId = tavernList;
            context.musicPlaying = true;
            context.runningLoops = juce::StringArray { "Rain", "Fire" };

            auto plan = planScene(combat, context);
            check(plan.switchPlaylist && plan.playlistId == combatList,
                   "a scene switches to its playlist when something else is playing");
            check(plan.loopsToStart == juce::StringArray { "Wind" },
                   "only the scene's loops that aren't already running are started");
            check(plan.loopsToStop == juce::StringArray { "Fire" },
                   "loops the scene doesn't list are stopped - a scene is the complete set");
            check(! plan.loopsToStart.contains("Rain") && ! plan.loopsToStop.contains("Rain"),
                   "a loop both scenes share is left running untouched, so it doesn't hiccup");
            check(! plan.setVolume, "a scene leaves the master volume alone unless told otherwise");

            // Now in combat. Pressing Combat again must not restart the
            // fight music - re-activating a playlist crossfades and jumps.
            context.activePlaylistId = combatList;
            context.runningLoops = juce::StringArray { "Rain", "Wind" };
            check(! planScene(combat, context).changesAnything(),
                   "pressing the scene already in effect changes nothing - the music is not restarted");

            // The Killswitch cut the ambience; the scene puts it back.
            context.runningLoops.clear();
            plan = planScene(combat, context);
            check(! plan.switchPlaylist && plan.loopsToStart == juce::StringArray { "Rain", "Wind" },
                   "pressing it again after the Killswitch brings the loops back and leaves the music alone");

            // The music was stopped (or is fading away): the scene brings
            // it back.
            context.musicPlaying = false;
            context.runningLoops = juce::StringArray { "Rain", "Wind" };
            check(planScene(combat, context).switchPlaylist,
                   "and it brings its music back if that had stopped");

            // Parts of a scene that have gone.
            Scene broken;
            broken.music = Scene::Music::playPlaylist;
            broken.playlistId = deletedList;
            broken.loops = juce::StringArray { "Rain", "Gone", "Clang" };
            context.musicPlaying = true;
            context.runningLoops.clear();

            plan = planScene(broken, context);
            check(! plan.switchPlaylist && plan.problems.size() == 3,
                   "a deleted playlist, a missing button and a button that no longer loops are each reported");
            check(plan.loopsToStart == juce::StringArray { "Rain" },
                   "and everything else in the scene still happens");

            Scene silence;
            silence.music = Scene::Music::fadeOut;
            context.musicPlaying = true;
            check(planScene(silence, context).fadeOutMusic, "a silence scene fades the music out");
            context.musicPlaying = false;
            check(! planScene(silence, context).fadeOutMusic,
                   "and does nothing to music that is already silent");

            Scene ambienceOnly;
            ambienceOnly.music = Scene::Music::leave;
            ambienceOnly.loops = juce::StringArray { "Rain" };
            context.musicPlaying = true;
            plan = planScene(ambienceOnly, context);
            check(! plan.switchPlaylist && ! plan.fadeOutMusic,
                   "\"leave the music alone\" really leaves it alone");

            Scene loud;
            loud.setsVolume = true;
            loud.volume = 1.5f;
            plan = planScene(loud, context);
            check(plan.setVolume && juce::approximatelyEqual(plan.volume, 1.0f),
                   "a scene that sets the volume does, and never past full");
        }

        {
            // The scene list itself: names, persistence, and following a
            // renamed soundboard button.
            auto sceneFile = scratch.getChildFile("scenes.json");
            sceneFile.deleteFile();

            SceneLibrary scenes;
            scenes.setFile(sceneFile);
            scenes.load();
            check(scenes.getNumScenes() == 0 && scenes.getLoadWarnings().isEmpty(),
                   "no scenes file yet is an empty list, not a problem");

            juce::Uuid tavernList;
            Scene tavern;
            tavern.name = "Tavern";
            tavern.colourArgb = 0xff8c2f2f;
            tavern.music = Scene::Music::playPlaylist;
            tavern.playlistId = tavernList;
            tavern.loops = juce::StringArray { "Fire", "Crowd" };
            tavern.setsVolume = true;
            tavern.volume = 0.6f;
            auto tavernId = scenes.add(tavern);

            Scene clash;
            clash.name = "tavern";
            auto clashId = scenes.add(clash);
            check(scenes.findById(clashId)->name == "tavern (2)",
                   "a scene name that clashes, ignoring case, is made unique - a Stream Deck finds scenes by name");

            SceneLibrary reloaded;
            reloaded.setFile(sceneFile);
            reloaded.load();
            auto* back = reloaded.findById(tavernId);
            check(back != nullptr && back->name == "Tavern" && back->colourArgb == 0xff8c2f2f
                   && back->music == Scene::Music::playPlaylist && back->playlistId == tavernList
                   && back->loops == juce::StringArray { "Fire", "Crowd" }
                   && back->setsVolume && juce::approximatelyEqual(back->volume, 0.6f),
                   "every part of a scene survives a restart");

            check(scenes.findByName("Tavern") != nullptr && scenes.findByName("Tavern")->id == tavernId,
                   "a scene is found by its exact name");
            check(scenes.findByName("  TAVERN ") != nullptr && scenes.findByName("  TAVERN ")->id == tavernId,
                   "and by its name typed in any case, since names are unique ignoring case");
            check(scenes.findByName("Nowhere") == nullptr, "a name that matches nothing finds nothing");

            check(scenes.renameLoop("Fire", "Hearth") == 1,
                   "renaming a soundboard button updates the scenes that use it");
            SceneLibrary afterRename;
            afterRename.setFile(sceneFile);
            afterRename.load();
            check(afterRename.findById(tavernId)->loops == juce::StringArray { "Hearth", "Crowd" },
                   "and that survives a restart");

            auto renamed = *scenes.findById(clashId);
            renamed.name = "TAVERN";
            check(! scenes.update(renamed), "a scene can't be renamed to another scene's name");

            scenes.move(clashId, -1);
            check(scenes.getScene(0)->id == clashId, "a scene can be moved earlier");

            scenes.remove(clashId);
            check(scenes.getNumScenes() == 1 && scenes.findById(clashId) == nullptr, "a scene can be deleted");

            // A newer version's file, and an unreadable one, are left
            // strictly alone - even after an edit is made here.
            for (auto contents : { juce::String(R"({"schemaVersion": 99, "scenes": []})"),
                                   juce::String("this is not json") })
            {
                auto untouchable = scratch.getChildFile("scenes-untouchable.json");
                untouchable.replaceWithText(contents);

                SceneLibrary guarded;
                guarded.setFile(untouchable);
                guarded.load();
                check(guarded.getNumScenes() == 0 && ! guarded.getLoadWarnings().isEmpty(),
                       "a scenes file this version can't use is reported, not loaded");

                Scene attempt;
                attempt.name = "Attempt";
                guarded.add(attempt);
                check(untouchable.loadFileAsString() == contents,
                       "and it is NOT overwritten by a later edit - its scenes aren't lost");
            }
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

            // Trims and pictures took the schema to 2; the loop flag
            // took it to 3. An older build must refuse the file rather
            // than rewrite it without what it doesn't understand.
            check(SoundboardLayout::kCurrentSchemaVersion == 3,
                   "the soundboard schema version was bumped for the new fields");
            check(boardFile2.loadFileAsString().contains("\"schemaVersion\": 3"),
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
                // Rearranging the board. The rule that matters is that a
                // slot's NAME travels with it: the engine keys sounds by
                // name and a Stream Deck button sends one, so a move that
                // renamed anything would silently retarget real hardware.
                SoundboardLayout board(formatManager);
                board.setFile(scratch.getChildFile("swap-board.json"));
                board.load();
                board.assign(0, folderTracks[0], "Thunder");
                board.assign(1, folderTracks[1], "Rain");
                board.setGainDb(0, -6.0f);
                board.setLoop(1, true);

                check(board.swapSlots(0, 1), "two filled buttons swap");
                check(board.getSlot(0).name == "Rain" && board.getSlot(1).name == "Thunder",
                       "each button's NAME travels with it, so Stream Deck buttons keep working");
                check(board.getSlot(1).gainDb == -6.0f,
                       "so does its volume trim");
                check(board.getSlot(0).loop,
                       "so does whether it loops");

                // Dragging onto an empty button is how a sound MOVES
                // rather than swapping with something.
                check(board.swapSlots(0, 5), "a button can be dragged onto an empty one");
                check(board.getSlot(0).isEmpty() && board.getSlot(5).name == "Rain",
                       "which moves it there and leaves the old place empty");

                check(! board.swapSlots(3, 3), "dropping a button on itself does nothing");
                check(! board.swapSlots(0, -1) && ! board.swapSlots(0, 9999),
                       "a drop outside the board does nothing rather than throwing");

                SoundboardLayout reloaded(formatManager);
                reloaded.setFile(scratch.getChildFile("swap-board.json"));
                reloaded.load();
                check(reloaded.getSlot(5).name == "Rain" && reloaded.getSlot(5).loop,
                       "a rearranged board, and its loop flags, survive a restart");
            }

            {
                // Looping slots: the engine half. A looping sound is a
                // LATCH - the same trigger that starts it stops it - so
                // one button, one Stream Deck press and one keyboard key
                // all behave the same way with nothing extra wired up.
                SoundboardEngine engine(formatManager);
                engine.prepareToPlay(512, 44100.0);

                engine.registerSound("Rain", folderTracks[0], 1.0f, true);
                engine.registerSound("Clang", folderTracks[1], 1.0f, false);

                juce::AudioBuffer<float> buffer(2, 512);
                juce::AudioSourceChannelInfo info(&buffer, 0, 512);
                auto pump = [&](int blocks)
                {
                    for (int i = 0; i < blocks; ++i)
                    {
                        buffer.clear();
                        engine.getNextAudioBlock(info);
                    }
                };

                check(! engine.isPlaying("Rain"), "nothing is playing before anything is triggered");

                engine.trigger("Rain");
                pump(5);
                check(engine.isPlaying("Rain"), "triggering a looping sound starts it");
                check(engine.getPlayingLoopNames().contains("Rain"),
                       "and the board can ask which loops are running");

                engine.trigger("Rain");
                pump(5);
                check(! engine.isPlaying("Rain"), "triggering it again stops it");

                // A one-shot must NOT have become a toggle: pressing a
                // sword clash twice in a row is supposed to play it twice.
                engine.trigger("Clang");
                pump(2);
                engine.trigger("Clang");
                pump(2);
                check(engine.getPlayingLoopNames().isEmpty(),
                       "a one-shot is never reported as a running loop");

                engine.trigger("Rain");
                pump(5);
                engine.stop("Rain");
                pump(2);
                check(! engine.isPlaying("Rain"), "and it can be stopped outright");

                engine.trigger("Rain");
                pump(5);
                engine.stopAllVoices();
                pump(2);
                check(! engine.isPlaying("Rain"),
                       "the panic button stops a running loop too - otherwise it would be the one "
                       "thing Stop all couldn't silence");
            }

            {
                // Scene changes: loops that FADE, and "make this true"
                // rather than toggle. Real time has to pass for a fade, so
                // the message loop is run for it.
                SoundboardEngine engine(formatManager);
                engine.prepareToPlay(512, 44100.0);
                engine.registerSound("Rain", folderTracks[0], 1.0f, true);
                engine.registerSound("Clang", folderTracks[1], 1.0f, false);

                auto wait = [](int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); };

                engine.startLoop("Rain", 0.5);
                check(engine.isPlaying("Rain") && engine.getFadeLevel("Rain") < 0.2f,
                       "a scene starts a loop from silence rather than at full volume");
                wait(900);
                check(juce::approximatelyEqual(engine.getFadeLevel("Rain"), 1.0f),
                       "and it fades all the way in");

                engine.startLoop("Rain", 0.5);
                check(juce::approximatelyEqual(engine.getFadeLevel("Rain"), 1.0f),
                       "starting a loop that's already running leaves it alone rather than restarting it");

                engine.stopLoop("Rain", 0.5);
                check(engine.isPlaying("Rain"), "stopping a loop for a scene fades it rather than cutting it");
                check(! engine.getPlayingLoopNames().contains("Rain"),
                       "and a loop on its way out no longer counts as running");

                wait(200); // partway down
                engine.startLoop("Rain", 0.5);
                wait(900);
                check(engine.isPlaying("Rain") && juce::approximatelyEqual(engine.getFadeLevel("Rain"), 1.0f),
                       "a loop wanted again mid-fade-out comes back up instead of stopping");

                engine.stopLoop("Rain", 0.3);
                wait(800);
                check(! engine.isPlaying("Rain"), "a loop faded all the way out really stops");

                engine.startLoop("Clang", 0.3);
                check(! engine.isPlaying("Clang"), "a scene never fires a one-shot");

                engine.startLoop("Rain", 2.0);
                wait(200);
                engine.stopAllVoices();
                check(! engine.isPlaying("Rain"), "the Killswitch still stops a loop at once, even mid-fade");
            }

            {
                // Scenes follow a renamed button through this hook, so it
                // has to fire - and only when the name really changed.
                SoundboardLayout board(formatManager);
                board.setFile(scratch.getChildFile("rename-hook-board.json"));
                board.load();
                board.assign(0, folderTracks[0], "Rain");

                juce::String seenOld, seenNew;
                board.onSlotRenamed = [&](const juce::String& o, const juce::String& n) { seenOld = o; seenNew = n; };

                board.rename(0, "Heavy Rain");
                check(seenOld == "Rain" && seenNew == "Heavy Rain",
                       "renaming a button reports its old and new names, so scenes can follow");

                seenOld = {};
                board.rename(0, "Heavy Rain");
                check(seenOld.isEmpty(), "\"renaming\" a button to the name it already has reports nothing");
            }

            {
                // beta.27.1: every store that refuses a file it can't use
                // must KEEP refusing after an edit. Each of these used to
                // refuse at load and then overwrite the file on the very
                // next change - every setter saves - destroying a newer
                // version's data, or a hand-edited file with a typo in it.
                const juce::String newer = R"({"schemaVersion": 99, "slots": [], "tracks": []})";
                const juce::String broken = "this is not json {";

                auto expectKept = [&](const juce::String& what, const juce::String& contents,
                                       const std::function<juce::StringArray(const juce::File&)>& loadAndEdit)
                {
                    auto file = scratch.getChildFile("protected-" + what.removeCharacters(" ") + ".json");
                    file.replaceWithText(contents);

                    auto warnings = loadAndEdit(file);
                    auto label = what + (contents == newer ? " from a newer version" : " that can't be read");

                    check(file.loadFileAsString() == contents,
                           "a " + label + " is NOT overwritten by a later edit");
                    check(! warnings.isEmpty() && warnings.joinIntoString(" ").contains("won't be saved"),
                           "and the warning says changes won't be saved, so it can't look like the app "
                           "forgetting them (" + label + ")");
                };

                for (auto contents : { newer, broken })
                {
                    expectKept("soundboard", contents, [&](const juce::File& file)
                    {
                        SoundboardLayout store(formatManager);
                        store.setFile(file);
                        store.load();
                        store.assign(0, folderTracks[0]);
                        store.setColour(0, 0xff8c2f2f);
                        return store.getLoadWarnings();
                    });

                    expectKept("track library", contents, [&](const juce::File& file)
                    {
                        TrackLibrary store;
                        store.setFile(file);
                        store.load();

                        // registerTracks, not registerTrack: the single
                        // form doesn't save by itself, so it would never
                        // reach the file and this check would prove
                        // nothing. This is the path the app actually uses.
                        juce::Array<juce::File> one;
                        one.add(folderTracks[0]);
                        store.registerTracks(one);
                        return store.getLoadWarnings();
                    });

                    expectKept("track volume file", contents, [&](const juce::File& file)
                    {
                        TrackSettingsStore store;
                        store.setFile(file);
                        store.load();
                        store.setGainDb(folderTracks[0], -3.0f);
                        return store.getLoadWarnings();
                    });
                }

                // The tag cache is the one deliberate exception, and only
                // half of it: a NEWER version's cache is kept, but an
                // unreadable one is rebuilt - it's only tags that can be
                // read again from the tracks, and refusing would stop it
                // ever recovering.
                auto cacheFile = scratch.getChildFile("protected-tagcache.json");

                cacheFile.replaceWithText(newer);
                {
                    TrackMetadataStore cache;
                    cache.setFile(cacheFile);
                    cache.load();
                    cache.save();
                }
                check(cacheFile.loadFileAsString() == newer,
                       "a tag cache from a newer version is NOT overwritten");

                cacheFile.replaceWithText(broken);
                {
                    TrackMetadataStore cache;
                    cache.setFile(cacheFile);
                    cache.load();
                    cache.save();
                }
                check(cacheFile.loadFileAsString() != broken,
                       "but an unreadable tag cache IS rebuilt - it's only a cache, and keeping it "
                       "would stop it ever recovering");
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

            {
                // Track lengths, for the Length columns. The tones are
                // exactly 8 seconds, which makes a real measurement
                // checkable rather than just "some number came back".
                auto measured = TrackMetadataStore::readFromFile(toneA, formatManager);
                check(measured.lengthRead && std::abs(measured.lengthMs - 8000) <= 50,
                       "a track's length is read along with its tags (8 s tone measured as "
                       + juce::String(measured.lengthMs) + " ms)");
                check(measured.displayLength() == "0:08", "and shows as 0:08");

                if (! folderTracks.isEmpty())
                {
                    auto real = TrackMetadataStore::readFromFile(folderTracks[0], formatManager);
                    check(real.lengthRead && real.lengthMs > 0,
                           "a real compressed track gets a length too");
                }

                TrackMetadata shown;
                shown.lengthRead = true;
                shown.lengthMs = 186700;
                check(shown.displayLength() == "3:07", "lengths round to the nearest second, like any player");
                shown.lengthMs = 59400;
                check(shown.displayLength() == "0:59", "under a minute shows as 0:ss");
                shown.lengthMs = 3765000;
                check(shown.displayLength() == "1:02:45", "an hour or more shows hours");
                shown.lengthMs = 0;
                check(shown.displayLength().isEmpty(),
                       "a length nobody could measure shows blank, not 0:00");
                shown.lengthRead = false;
                shown.lengthMs = 5000;
                check(shown.displayLength().isEmpty(), "and so does one that hasn't been read yet");

                // An existing library's cache has no lengths. Its entries
                // must come back as not-yet-read, so the next scan measures
                // them - otherwise unchanged files would never be re-read
                // and the column would stay blank for everything already
                // in someone's library.
                auto cacheFile = scratch.getChildFile("length-cache.json");
                auto key = toneA.getFullPathName().toLowerCase();
                auto oldCache = juce::String(R"({"schemaVersion": 1, "tracks": [{"path": )")
                                + juce::JSON::toString(key) + R"(, "title": "Tone", "size": 1, "modified": 1}]})";
                cacheFile.replaceWithText(oldCache);

                TrackMetadataStore oldStore;
                oldStore.setFile(cacheFile);
                oldStore.load();
                auto cached = oldStore.get(toneA);
                check(cached.scanned && cached.title == "Tone" && ! cached.lengthRead,
                       "a cache from before lengths loads its tags, and marks the length as not read yet");

                // Reading it fills the length in, and it survives a save.
                auto fresh = juce::String(R"({"schemaVersion": 1, "tracks": [{"path": )")
                             + juce::JSON::toString(key) + R"(, "title": "Tone", "lengthMs": 8000, "size": 1, "modified": 1}]})";
                cacheFile.replaceWithText(fresh);

                TrackMetadataStore newStore;
                newStore.setFile(cacheFile);
                newStore.load();
                newStore.save();

                TrackMetadataStore reloaded;
                reloaded.setFile(cacheFile);
                reloaded.load();
                check(reloaded.get(toneA).lengthRead && reloaded.get(toneA).lengthMs == 8000,
                       "a cached length survives a save and reload");
                check(cacheFile.loadFileAsString().contains("\"schemaVersion\": 1"),
                       "and the cache version is unchanged, so older builds still read it");
            }

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

                // A new list asked for during a fade-out. The fade used to
                // carry on regardless and stop the NEW music as well when
                // it reached the bottom - press Fade out, then pick a
                // playlist or a scene, and nothing played.
                engine.fadeOutAndStop(1.0);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
                check(engine.isFadingOut(), "a fade-out is under way (setting up the next check)");

                juce::Array<juce::File> justA;
                justA.add(toneA);
                engine.crossfadeToTracks(justA, {});
                check(! engine.isFadingOut(), "starting a new list during a fade-out takes over from the fade");

                // Well past where the abandoned fade-out would have ended.
                juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);
                check(engine.isPlaying() && magnitude.load() > 0.01f,
                       "so the new music keeps playing instead of being faded out along with the old");

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

    // INKWYRD_SCENESNAPSHOT=<folder>: draws the Scenes window's content
    // and the scene editor to PNGs, off-screen, with the app's own look
    // and feel. A way to SEE new UI without launching a second copy of
    // the app or driving anyone's mouse - neither is acceptable while the
    // user may be at the machine. Uses a scratch scenes file, never the
    // real one.
    // INKWYRD_SKINRENDER=<folder> [INKWYRD_SKIN=<skin folder>]: paints a
    // gallery of the app's stock widgets - a skinned window with its title
    // bar, the Player's transport, toggles and sliders by their real
    // component IDs, plus fields, lists, panels and the display well -
    // through the real InkwyrdLookAndFeel, once in the built-in look and
    // once in the given skin, at 1x and 2x.
    //
    // For judging sprite art without launching the app: the app shares
    // %APPDATA% with the user's own session, and a skin under development
    // changes every few minutes.
    class SkinGallery : public juce::Component
    {
    public:
        SkinGallery()
        {
            auto addButton = [this](juce::TextButton& b, const char* text, const char* id)
            {
                b.setButtonText(text);
                b.setComponentID(id);
                addAndMakeVisible(b);
            };

            addButton(play, "Play", "transport.play");
            addButton(stop, "Stop", "transport.stop");
            addButton(fade, "Fade out", "transport.fadeout");
            addButton(skip, "Skip", "transport.skip");
            addButton(shuffle, "Shuffle: On", "toggle.shuffle");
            shuffle.setToggleState(true, juce::dontSendNotification);

            addButton(mute, "Mic: Muted", "toggle.mute");
            mute.setToggleState(true, juce::dontSendNotification);
            mute.setColour(juce::TextButton::buttonOnColourId, inkwyrd::theme::warning.withAlpha(0.35f));
            mute.setColour(juce::TextButton::textColourOnId, inkwyrd::theme::warning);
            addButton(monitor, "Monitor: Off", "toggle.monitor");
            addButton(crossfadeToggle, "On", "toggle.crossfade");
            crossfadeToggle.setToggleState(true, juce::dontSendNotification);
            addButton(loopToggle, "Off", "toggle.loop");

            for (auto* label : { &crossfadeCaption, &loopCaption, &masterCaption, &micCaption })
                addAndMakeVisible(label);

            auto setUpSlider = [this](juce::Slider& s, const char* id, double value, const char* suffix)
            {
                s.setComponentID(id);
                s.setSliderStyle(juce::Slider::LinearHorizontal);
                s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 22);
                s.setRange(0.0, 100.0, 1.0);
                s.setValue(value, juce::dontSendNotification);
                s.setTextValueSuffix(suffix);
                addAndMakeVisible(s);
            };

            setUpSlider(crossfade, "crossfade", 30.0, " s");
            setUpSlider(loopGap, "loopgap", 0.0, " s");
            loopGap.setEnabled(false);
            setUpSlider(master, "master", 80.0, "%");
            setUpSlider(mic, "mic", 55.0, "%");

            addButton(openPlaylist, "Playlist", "open.playlist");
            addButton(openLibrary, "Library", "open.library");
            addButton(openVoiceFx, "Voice FX", "open.voicefx");
            addButton(openSoundboard, "Soundboard", "open.soundboard");
            addButton(openScenes, "Scenes", "open.scenes");

            // One generic button per state, since a snapshot can't hover.
            const char* stateNames[] = { "Normal", "Hover", "Pressed", "Disabled", "On" };
            for (int i = 0; i < 5; ++i)
            {
                auto* b = stateButtons.add(new juce::TextButton(stateNames[i]));
                addAndMakeVisible(b);
            }
            stateButtons[1]->setState(juce::Button::buttonOver);
            stateButtons[2]->setState(juce::Button::buttonDown);
            stateButtons[3]->setEnabled(false);
            stateButtons[4]->setToggleState(true, juce::dontSendNotification);

            tickOn.setToggleState(true, juce::dontSendNotification);
            addAndMakeVisible(tickOn);
            addAndMakeVisible(tickOff);

            field.setText("Tavern ambience");
            addAndMakeVisible(field);

            combo.addItem("Pixel Phosphor", 1);
            combo.setSelectedId(1, juce::dontSendNotification);
            addAndMakeVisible(combo);

            list.setModel(&model);
            list.setRowHeight(20);
            addAndMakeVisible(list);

            setSize(760, 470);
        }

        ~SkinGallery() override { list.setModel(nullptr); }

        void paint(juce::Graphics& g) override
        {
            InkwyrdLookAndFeel::drawInsetWell(g, displayArea, "display");

            g.setColour(inkwyrd::theme::accent);
            g.setFont(InkwyrdLookAndFeel::digitFont(28.0f));
            g.drawText("01:23", displayArea.reduced(14, 10).removeFromTop(34), juce::Justification::topLeft);
            g.setFont(InkwyrdLookAndFeel::labelFont(15.0f));
            g.setColour(inkwyrd::theme::text);
            g.drawText("The Wyrm's Rest - Hearthfire", displayArea.reduced(14, 10).withTrimmedTop(40),
                        juce::Justification::topLeft);

            InkwyrdLookAndFeel::drawPanel(g, panelArea, false);
            InkwyrdLookAndFeel::drawPanel(g, raisedArea, true);
            InkwyrdLookAndFeel::drawInsetWell(g, wellArea);

            g.setColour(inkwyrd::theme::textDim);
            g.setFont(InkwyrdLookAndFeel::labelFont(12.0f));
            g.drawText("panel", panelArea, juce::Justification::centred);
            g.drawText("panel.raised", raisedArea, juce::Justification::centred);
            g.drawText("well", wellArea, juce::Justification::centred);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(14);
            auto right = area.removeFromRight(200);
            area.removeFromRight(14);

            displayArea = area.removeFromTop(86);
            area.removeFromTop(10);

            auto row = [&area](int h) { auto r = area.removeFromTop(h); area.removeFromTop(8); return r; };

            auto r1 = row(32);
            for (auto* b : { &play, &stop, &fade, &skip })
            {
                b->setBounds(r1.removeFromLeft(72));
                r1.removeFromLeft(6);
            }
            shuffle.setBounds(r1.removeFromLeft(110));

            auto r2 = row(28);
            mute.setBounds(r2.removeFromLeft(100));
            r2.removeFromLeft(6);
            monitor.setBounds(r2.removeFromLeft(110));
            r2.removeFromLeft(10);
            crossfadeCaption.setBounds(r2.removeFromLeft(70));
            crossfadeToggle.setBounds(r2.removeFromLeft(46));
            r2.removeFromLeft(4);
            crossfade.setBounds(r2);

            auto r3 = row(28);
            loopCaption.setBounds(r3.removeFromLeft(70));
            loopToggle.setBounds(r3.removeFromLeft(46));
            r3.removeFromLeft(4);
            loopGap.setBounds(r3.removeFromLeft(200));

            auto r4 = row(28);
            micCaption.setBounds(r4.removeFromLeft(40));
            mic.setBounds(r4.removeFromLeft(190));
            r4.removeFromLeft(10);
            masterCaption.setBounds(r4.removeFromLeft(56));
            master.setBounds(r4);

            auto r5 = row(28);
            auto w = (r5.getWidth() - 4 * 6) / 5;
            for (auto* b : { &openPlaylist, &openLibrary, &openVoiceFx, &openSoundboard, &openScenes })
            {
                b->setBounds(r5.removeFromLeft(w));
                r5.removeFromLeft(6);
            }

            auto r6 = row(28);
            for (auto* b : stateButtons)
            {
                b->setBounds(r6.removeFromLeft(w));
                r6.removeFromLeft(6);
            }

            auto r7 = area.removeFromTop(64);
            panelArea = r7.removeFromLeft(r7.getWidth() / 3).reduced(3);
            raisedArea = r7.removeFromLeft(r7.getWidth() / 2).reduced(3);
            wellArea = r7.reduced(3);

            tickOn.setBounds(right.removeFromTop(26));
            tickOff.setBounds(right.removeFromTop(26));
            right.removeFromTop(8);
            field.setBounds(right.removeFromTop(28));
            right.removeFromTop(8);
            combo.setBounds(right.removeFromTop(28));
            right.removeFromTop(8);
            list.setBounds(right);
        }

    private:
        struct Model : juce::ListBoxModel
        {
            int getNumRows() override { return 40; }
            void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) override
            {
                g.setColour(row == 2 ? inkwyrd::theme::accent : inkwyrd::theme::text);
                g.setFont(InkwyrdLookAndFeel::labelFont(13.0f));
                g.drawText("Track " + juce::String(row + 1), 6, 0, w - 12, h, juce::Justification::centredLeft);
            }
        } model;

        juce::TextButton play, stop, fade, skip, shuffle, mute, monitor, crossfadeToggle, loopToggle;
        juce::TextButton openPlaylist, openLibrary, openVoiceFx, openSoundboard, openScenes;
        juce::OwnedArray<juce::TextButton> stateButtons;
        juce::Label crossfadeCaption { {}, "Crossfade" }, loopCaption { {}, "Loop track" };
        juce::Label masterCaption { {}, "Master" }, micCaption { {}, "Mic" };
        juce::Slider crossfade, loopGap, master, mic;
        juce::ToggleButton tickOn { "Tick box, on" }, tickOff { "Tick box, off" };
        juce::TextEditor field;
        juce::ComboBox combo;
        juce::ListBox list;
        juce::Rectangle<int> displayArea, panelArea, raisedArea, wellArea;
    };

    class GalleryWindow : public juce::DocumentWindow, public InkwyrdLookAndFeel::TitleBarInfo
    {
    public:
        GalleryWindow()
            : DocumentWindow("Inkwyrd Audio", inkwyrd::theme::panel, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(false);
            setTitleBarHeight(inkwyrd::theme::titleBarHeight);
            setContentNonOwned(&gallery, true);
        }

        juce::String getTitleBarSubtitle() const override { return "Audio Player"; }

    private:
        SkinGallery gallery;
    };

    int runSkinRender(const juce::File& folder, const juce::File& skinFolder)
    {
        juce::ScopedJuceInitialiser_GUI gui;
        InkwyrdLookAndFeel lookAndFeel;
        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
        folder.createDirectory();

        auto render = [&folder](const juce::String& name, float scale)
        {
            // Built fresh for each look, like a window opened after the
            // skin was chosen - title bar height is read at construction.
            GalleryWindow window;
            auto image = window.createComponentSnapshot(window.getLocalBounds(), true, scale);

            // Paint cost, at the size of a real Player window. A live
            // resize repaints on every mouse move, and Windows shows blank
            // (white) new area until the paint lands - so a slow paint IS
            // the resize "ghost". Software renderer, so a relative number:
            // compare looks, not absolute milliseconds.
            window.setSize(800, 750);
            constexpr int runs = 20;
            auto start = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < runs; ++i)
                window.createComponentSnapshot(window.getLocalBounds(), true, scale);
            std::cout << "  " << name << ": " << juce::String((juce::Time::getMillisecondCounterHiRes() - start) / runs, 1)
                      << " ms per full paint at 800x750" << std::endl;

            auto file = folder.getChildFile(name + ".png");
            file.deleteFile();

            juce::FileOutputStream out(file);
            auto ok = out.openedOk() && juce::PNGImageFormat().writeImageToStream(image, out);
            std::cout << (ok ? "" : "FAILED ") << file.getFullPathName() << std::endl;
            return ok;
        };

        auto ok = render("gallery-builtin", 1.0f);

        if (skinFolder != juce::File())
        {
            auto result = inkwyrd::SkinLoader::loadFromFolder(skinFolder);
            if (! result.ok)
            {
                std::cout << "Skin didn't load: " << result.message << std::endl;
                return 1;
            }

            for (auto& warning : result.warnings)
                std::cout << "  warning: " << warning << std::endl;

            std::cout << "  " << result.sprites.size() << " sprite(s)" << std::endl;

            inkwyrd::theme::applyPalette(result.palette);
            inkwyrd::setActiveSprites(std::move(result.sprites));
            juce::String logoError;
            if (! InkwyrdLookAndFeel::setSkinLogo(result.logoFile, logoError))
                std::cout << "  logo: " << logoError << std::endl;
            lookAndFeel.refreshColours();

            ok = render("gallery-skin", 1.0f) && ok;
            ok = render("gallery-skin@2x", 2.0f) && ok;

            // Back to the built-in look, so nothing static outlives the run.
            inkwyrd::setActiveSprites({});
            InkwyrdLookAndFeel::setSkinLogo({}, logoError);
        }

        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        return ok ? 0 : 1;
    }

    int runSceneSnapshot(const juce::File& folder)
    {
        juce::ScopedJuceInitialiser_GUI gui;
        InkwyrdLookAndFeel lookAndFeel;
        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
        folder.createDirectory();

        auto write = [&folder](juce::Component& component, const juce::String& name)
        {
            auto image = component.createComponentSnapshot(component.getLocalBounds(), true, 1.0f);
            auto file = folder.getChildFile(name + ".png");
            file.deleteFile();

            juce::FileOutputStream out(file);
            auto ok = out.openedOk() && juce::PNGImageFormat().writeImageToStream(image, out);
            std::cout << (ok ? "" : "FAILED ") << file.getFullPathName() << std::endl;
            return ok;
        };

        auto scratchFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("inkwyrd-scene-snapshot.json");
        scratchFile.deleteFile();

        auto okCount = 0;

        {
            // Empty: the first thing anyone sees.
            SceneLibrary empty;
            empty.setFile(scratchFile);
            empty.load();

            ScenesComponent scenes(empty, {});
            scenes.setSize(560, 240);
            okCount += write(scenes, "scenes-empty") ? 1 : 0;
        }

        juce::Uuid activeId;
        SceneLibrary library;
        library.setFile(scratchFile);
        library.load();

        auto addScene = [&library](const juce::String& name, juce::uint32 colour)
        {
            Scene scene;
            scene.name = name;
            scene.colourArgb = colour;
            return library.add(scene);
        };

        addScene("Tavern", 0xff9c5a1e);
        addScene("Road", 0xff2a3a33);
        activeId = addScene("Combat", 0xff8c2f2f);
        addScene("Crypt", 0xff5a2f8c);
        auto brokenId = addScene("Storm at Sea", 0xff1e6b6b);
        addScene("Silence", 0xff2f4a8c);

        {
            ScenesComponent::Callbacks callbacks;
            callbacks.problemsFor = [brokenId](const Scene& scene)
            {
                return scene.id == brokenId ? juce::StringArray { "\"Waves\" isn't on the soundboard any more" }
                                            : juce::StringArray();
            };

            ScenesComponent scenes(library, callbacks);
            scenes.setSize(560, 240);
            scenes.setActiveScene(activeId);
            okCount += write(scenes, "scenes-full") ? 1 : 0;
        }

        {
            // The editor as it opens after "Save current as scene": a
            // playlist picked, two loops running, one the board has lost.
            Scene captured;
            captured.name = "Tavern";
            captured.music = Scene::Music::playPlaylist;
            juce::Uuid tavernList;
            captured.playlistId = tavernList;
            captured.loops = juce::StringArray { "Fire", "Crowd", "Old Rain" };
            captured.setsVolume = true;
            captured.volume = 0.7f;

            std::vector<SceneEditor::PlaylistChoice> playlists { { juce::Uuid(), "Combat" },
                                                                   { tavernList, "Tavern Night" },
                                                                   { juce::Uuid(), "Exploration" } };

            SceneEditor editor(captured, playlists, juce::StringArray { "Fire", "Crowd", "Rain", "Wind" }, {});
            okCount += write(editor, "scene-editor") ? 1 : 0;
        }

        {
            // And with no looping buttons at all, where the hint shows.
            Scene blank;
            blank.name = "Scene 1";
            SceneEditor editor(blank, {}, {}, {});
            okCount += write(editor, "scene-editor-no-loops") ? 1 : 0;
        }

        scratchFile.deleteFile();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        return okCount == 4 ? 0 : 1;
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
    // Decodes the first stretch of a file so a before/after comparison
    // can prove that tagging didn't touch a single audio sample. This is
    // the check that matters: a tagger that corrupts files is worse than
    // no tagger at all.
    bool decodeToBuffer(const juce::File& file, juce::AudioFormatManager& formats,
                         juce::AudioBuffer<float>& destination)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr)
            return false;

        auto samples = (int) juce::jmin((juce::int64) (reader->sampleRate * 20.0),
                                         reader->lengthInSamples);
        if (samples <= 0)
            return false;

        destination.setSize((int) reader->numChannels, samples);
        reader->read(&destination, 0, samples, 0, true, true);
        return true;
    }

    juce::MemoryBlock makeTestPng()
    {
        juce::Image image(juce::Image::RGB, 8, 8, true);
        juce::Graphics g(image);
        g.fillAll(juce::Colours::rebeccapurple);

        juce::MemoryBlock bytes;
        juce::MemoryOutputStream stream(bytes, false);
        juce::PNGImageFormat().writeImageToStream(image, stream);
        stream.flush();

        return bytes;
    }

    // Writing tags means rewriting someone's music files, so these run
    // against real files of the formats people actually have - copied
    // into a scratch folder first. The user's own library is never
    // written to.
    void runTagWriteChecks(const juce::Array<juce::File>& tracks, juce::AudioFormatManager& formats)
    {
        auto scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("inkwyrd-tag-write-test");
        scratch.deleteRecursively();
        scratch.createDirectory();

        juce::File sourceMp3, sourceFlac;
        for (const auto& file : tracks)
        {
            if (! file.existsAsFile())
                continue;

            auto extension = file.getFileExtension().toLowerCase();
            if (sourceMp3 == juce::File() && extension == ".mp3")
                sourceMp3 = file;
            if (sourceFlac == juce::File() && extension == ".flac")
                sourceFlac = file;
        }

        juce::Array<juce::File> sources;
        if (sourceMp3 != juce::File()) sources.add(sourceMp3);
        if (sourceFlac != juce::File()) sources.add(sourceFlac);

        check(! sources.isEmpty(), "found a real file to test tag WRITING against");

        for (const auto& source : sources)
        {
            auto label = source.getFileExtension().toLowerCase();
            auto copy = scratch.getChildFile("subject" + label);
            source.copyFileTo(copy);

            juce::AudioBuffer<float> before, after;
            auto decodedBefore = decodeToBuffer(copy, formats, before);
            check(decodedBefore, label + ": the copy decodes before tagging");

            inkwyrd::TagChanges changes;
            changes.title = inkwyrd::TagField::setTo("Inkwyrd Test Title");
            changes.artist = inkwyrd::TagField::setTo("Inkwyrd Test Artist");
            changes.album = inkwyrd::TagField::setTo("Inkwyrd Test Album");
            changes.albumArtist = inkwyrd::TagField::setTo("Inkwyrd Album Artist");
            changes.genre = inkwyrd::TagField::setTo("Ambient");
            changes.comment = inkwyrd::TagField::setTo("written by the self-test");
            changes.composer = inkwyrd::TagField::setTo("A Composer");
            changes.publisher = inkwyrd::TagField::setTo("A Label");
            changes.year = inkwyrd::TagField::setTo("1979");
            changes.trackNumber = inkwyrd::TagField::setTo("3");
            changes.trackTotal = inkwyrd::TagField::setTo("12");
            changes.discNumber = inkwyrd::TagField::setTo("1");
            changes.discTotal = inkwyrd::TagField::setTo("2");
            changes.bpm = inkwyrd::TagField::setTo("120");
            changes.artworkAction = inkwyrd::TagChanges::ArtworkAction::set;
            changes.artwork = makeTestPng();
            changes.artworkMimeType = "image/png";

            juce::String error;
            check(inkwyrd::TagEditor::write(copy, changes, error),
                   label + ": every field writes (" + error + ")");

            auto written = inkwyrd::TagEditor::read(copy);
            check(written.title == "Inkwyrd Test Title" && written.artist == "Inkwyrd Test Artist"
                   && written.album == "Inkwyrd Test Album" && written.albumArtist == "Inkwyrd Album Artist"
                   && written.genre == "Ambient" && written.composer == "A Composer"
                   && written.publisher == "A Label" && written.year == "1979"
                   && written.bpm == "120",
                   label + ": every field reads back as written");
            check(written.trackNumber == "3" && written.trackTotal == "12"
                   && written.discNumber == "1" && written.discTotal == "2",
                   label + ": track and disc keep their number AND total");
            check(written.artwork.getSize() == changes.artwork.getSize(),
                   label + ": artwork round-trips");

            auto decodedAfter = decodeToBuffer(copy, formats, after);
            check(decodedAfter, label + ": the file still decodes after tagging");

            auto sameAudio = decodedBefore && decodedAfter
                              && before.getNumChannels() == after.getNumChannels()
                              && before.getNumSamples() == after.getNumSamples();

            if (sameAudio)
                for (int channel = 0; channel < before.getNumChannels() && sameAudio; ++channel)
                    for (int i = 0; i < before.getNumSamples(); ++i)
                        if (before.getSample(channel, i) != after.getSample(channel, i))
                        {
                            sameAudio = false;
                            break;
                        }

            check(sameAudio, label + ": TAGGING CHANGED NO AUDIO - every sample identical");

            // One field at a time: the rest of the tags must survive
            // untouched, which is what "leave" has to mean.
            inkwyrd::TagChanges justTheTitle;
            justTheTitle.title = inkwyrd::TagField::setTo("Second Pass");
            check(inkwyrd::TagEditor::write(copy, justTheTitle, error),
                   label + ": a single-field edit writes");

            auto second = inkwyrd::TagEditor::read(copy);
            check(second.title == "Second Pass", label + ": the edited field changed");
            check(second.artist == "Inkwyrd Test Artist" && second.album == "Inkwyrd Test Album"
                   && second.trackTotal == "12",
                   label + ": and every field left alone is untouched");

            inkwyrd::TagChanges clearComment;
            clearComment.comment = inkwyrd::TagField::cleared();
            clearComment.artworkAction = inkwyrd::TagChanges::ArtworkAction::clear;
            check(inkwyrd::TagEditor::write(copy, clearComment, error),
                   label + ": clearing writes");

            auto cleared = inkwyrd::TagEditor::read(copy);
            check(cleared.comment.isEmpty(), label + ": a cleared field is really gone");
            check(cleared.artwork.getSize() == 0, label + ": cleared artwork is really gone");
            check(cleared.title == "Second Pass", label + ": clearing one field leaves the others");
        }

        // A file nothing can tag is refused with a message rather than
        // half-written or silently ignored.
        auto notAudio = scratch.getChildFile("notes.txt");
        notAudio.replaceWithText("not audio");
        inkwyrd::TagChanges anything;
        anything.title = inkwyrd::TagField::setTo("nope");
        juce::String refusal;
        check(! inkwyrd::TagEditor::write(notAudio, anything, refusal) && refusal.isNotEmpty(),
               "a file that can't carry tags is refused, with a reason");
        check(notAudio.loadFileAsString() == "not audio", "and that file is left exactly as it was");

        scratch.deleteRecursively();
    }

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

        runTagWriteChecks(tracks, formats);

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

// See the INKWYRD_UPDATECHECK branch in main().
static int runUpdateCheck()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Whatever this harness was built alongside. Printed too, because
    // "no update" is only meaningful next to what it compared against.
    //
    // Set INKWYRD_UPDATECHECK to a VERSION rather than 1 to pose as an
    // older build ("0.1.0-beta.1"). Without that, a run on the newest
    // release prints the same "nothing newer" as a run with no network
    // at all, and the request itself would never actually be proven.
    auto requested = juce::SystemStats::getEnvironmentVariable("INKWYRD_UPDATECHECK", "");
    const juce::String currentVersion = requested == "1" ? juce::String(INKWYRD_VERSION_STRING)
                                                          : requested;
    std::cout << "This build: " << currentVersion.toStdString() << std::endl;

    std::atomic<bool> answered { false };

    inkwyrd::checkForNewerRelease(currentVersion, [&answered](inkwyrd::ReleaseInfo release)
    {
        std::cout << "NEWER RELEASE: " << release.version.toStdString()
                   << "  " << release.url.toStdString() << std::endl;
        answered = true;
    });

    // The check is deliberately silent when there is nothing to say, so
    // this waits a fixed time rather than for an answer that may never
    // come.
    auto deadline = juce::Time::getMillisecondCounter() + 10000;
    while (juce::Time::getMillisecondCounter() < deadline && ! answered)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    if (! answered)
        std::cout << "No newer release reported (up to date, offline, or rate-limited)." << std::endl;

    return 0;
}

int runResizeFlashTest(); // ResizeFlashTest.cpp
int runResizeDragTest();  // ResizeFlashTest.cpp - moves the real mouse
int runResizeDragAppTest(); // ResizeFlashTest.cpp - drags a running Inkwyrd window

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

    // INKWYRD_UPDATECHECK=1: asks the REAL GitHub API, once, and prints
    // what it makes of the answer. Kept out of the default suite - the
    // self-test must not depend on being online, or on someone else's
    // rate limit - but this is the only way to check that the request
    // itself works: the User-Agent GitHub demands, the JSON shape it
    // really sends, and the verdict against this build's own version.
    if (juce::SystemStats::getEnvironmentVariable("INKWYRD_UPDATECHECK", "").isNotEmpty())
        return runUpdateCheck();

    // Reads the user's REAL library, so it needs no fixture folder and
    // goes above the PLAYLIST_FOLDER guard like the others.
    if (juce::SystemStats::getEnvironmentVariable("INKWYRD_TAGTEST", "").isNotEmpty())
        return runTagTest();

    auto sceneSnapshotFolder = juce::SystemStats::getEnvironmentVariable("INKWYRD_SCENESNAPSHOT", "");
    if (sceneSnapshotFolder.isNotEmpty())
        return runSceneSnapshot(juce::File(sceneSnapshotFolder));

    if (juce::SystemStats::getEnvironmentVariable("INKWYRD_RESIZETEST", "") == "dragapp")
        return runResizeDragAppTest();

    if (juce::SystemStats::getEnvironmentVariable("INKWYRD_RESIZETEST", "") == "drag")
        return runResizeDragTest();

    if (juce::SystemStats::getEnvironmentVariable("INKWYRD_RESIZETEST", "").isNotEmpty())
        return runResizeFlashTest();

    auto skinRenderFolder = juce::SystemStats::getEnvironmentVariable("INKWYRD_SKINRENDER", "");
    if (skinRenderFolder.isNotEmpty())
    {
        auto skin = juce::SystemStats::getEnvironmentVariable("INKWYRD_SKIN", "");
        return runSkinRender(juce::File(skinRenderFolder), skin.isNotEmpty() ? juce::File(skin) : juce::File());
    }

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
