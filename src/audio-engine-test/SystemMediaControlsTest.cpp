// Checks that the app really does publish to Windows' System Media
// Transport Controls, by PUBLISHING with the shipping SystemMediaControls
// class and then READING IT BACK through
// GlobalSystemMediaTransportControlsSessionManager - the same API every
// now-playing overlay uses. Run with INKWYRD_MEDIATEST=1.
//
// Its own file so windows.h and winrt stay out of Main.cpp, same reason
// as ResizeFlashTest.cpp.
//
// WHY A READ-BACK AND NOT A UNIT TEST. Nothing about this feature can be
// established by calling our own code and checking it didn't throw: every
// interesting failure here is one where we set something, Windows accepts
// it without complaint, and a consumer still sees nothing. That was not
// hypothetical - the first working build of this set the album art from a
// file:/// Uri, which succeeds, logs nothing, and leaves every consumer
// with a null thumbnail. Only reading it back from the other side catches
// that, so that is what this does.
//
// It deliberately publishes REAL files with REAL embedded artwork (copies
// of test-media/formats/song_mp3.mp3, tagged by the app's own TagEditor),
// because the art path crosses a thread and an async WinRT call and is
// where the mechanism is actually difficult.

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <winrt/base.h>
 #include <winrt/Windows.Foundation.h>
 #include <winrt/Windows.Foundation.Collections.h>
 #include <winrt/Windows.Media.Control.h>
 #include <winrt/Windows.Storage.Streams.h>
#endif

#include "SystemMediaControls.h"
#include "TagEditor.h"

namespace
{
#if JUCE_WINDOWS

    int failures = 0;

    void check(bool condition, const juce::String& what)
    {
        std::cout << (condition ? "  ok   " : "  FAIL ") << what << std::endl;

        if (! condition)
            ++failures;
    }

    void checkEquals(const juce::String& actual, const juce::String& expected, const juce::String& what)
    {
        check(actual == expected, what + " (expected '" + expected + "', got '" + actual + "')");
    }

    // Everything SMTC will tell a consumer about one session.
    struct Snapshot
    {
        bool found = false;

        // How many sessions Windows is reporting for THIS app. More than
        // one means a stale session is lingering, and then "the first
        // one that matches" is not necessarily the live one.
        int matching = 0;
        juce::String appId, title, artist, album, artContentType;
        int trackNumber = 0;
        int status = -1; // winrt MediaPlaybackStatus
        int artBytes = 0;
        bool artIsRealImage = false;
    };

    juce::String toJuce(const winrt::hstring& text)
    {
        return juce::String(text.c_str());
    }

    // A PNG starts 89 50 4E 47, a JPEG FF D8 FF. Checked so "some bytes
    // arrived" can't pass for "the actual cover arrived".
    bool looksLikeImage(const std::vector<uint8_t>& bytes)
    {
        if (bytes.size() > 4 && bytes[0] == 0x89 && bytes[1] == 0x50
             && bytes[2] == 0x4E && bytes[3] == 0x47)
            return true;

        return bytes.size() > 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF;
    }

    Snapshot readSessionOnThisThread(const juce::String& wantedAppId)
    {
        using namespace winrt::Windows::Media::Control;
        using namespace winrt::Windows::Storage::Streams;

        Snapshot result;

        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

        for (auto const& session : manager.GetSessions())
        {
            auto appId = toJuce(session.SourceAppUserModelId());

            if (appId != wantedAppId)
                continue;

            ++result.matching;

            if (result.found)
            {
                // Diagnostic only: a second session for the same app.
                auto extra = session.TryGetMediaPropertiesAsync().get();
                std::cout << "  (extra session: '" << toJuce(extra.Title())
                           << "' status " << (int) session.GetPlaybackInfo().PlaybackStatus()
                           << ")" << std::endl;
                continue;
            }

            result.found = true;
            result.appId = appId;
            result.status = (int) session.GetPlaybackInfo().PlaybackStatus();

            auto props = session.TryGetMediaPropertiesAsync().get();
            result.title = toJuce(props.Title());
            result.artist = toJuce(props.Artist());
            result.album = toJuce(props.AlbumTitle());
            result.trackNumber = (int) props.TrackNumber();

            if (auto thumbnail = props.Thumbnail())
            {
                auto stream = thumbnail.OpenReadAsync().get();
                const auto size = (uint32_t) stream.Size();

                DataReader reader(stream.GetInputStreamAt(0));
                reader.LoadAsync(size).get();

                std::vector<uint8_t> bytes(size);
                reader.ReadBytes(bytes);

                result.artBytes = (int) size;
                result.artContentType = toJuce(stream.ContentType());
                result.artIsRealImage = looksLikeImage(bytes);
            }
        }

        return result;
    }

    // The read has to happen off the message thread (blocking on a WinRT
    // async call from an STA is not allowed), but the message thread must
    // keep pumping while it does: anything owned by this process's STA
    // that the reader reaches through would otherwise deadlock waiting
    // for a thread that is sitting in join().
    Snapshot readSession(const juce::String& wantedAppId)
    {
        Snapshot result;
        std::atomic<bool> done { false };
        juce::String error;

        std::thread worker([&]
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);

            try
            {
                result = readSessionOnThisThread(wantedAppId);
            }
            catch (const winrt::hresult_error& thrown)
            {
                error = "0x" + juce::String::toHexString((int) thrown.code().value);
            }
            catch (...)
            {
                error = "unknown exception";
            }

            winrt::uninit_apartment();
            done = true;
        });

        const auto deadline = juce::Time::getMillisecondCounter() + 10000;

        while (! done.load() && juce::Time::getMillisecondCounter() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

        worker.join();

        if (error.isNotEmpty())
            std::cout << "  (reader error: " << error << ")" << std::endl;

        return result;
    }

    void pump(int milliseconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(milliseconds);
    }

    // Art crosses a worker thread and two async WinRT calls, so it
    // genuinely arrives after the text does. Waiting for a SIZE rather
    // than for "any art" is what makes this able to tell new art from
    // the previous track's.
    Snapshot readSessionWaitingForArt(const juce::String& appId, int expectedArtBytes)
    {
        Snapshot snapshot;

        for (int attempt = 0; attempt < 12; ++attempt)
        {
            snapshot = readSession(appId);

            if (snapshot.found && snapshot.artBytes == expectedArtBytes)
                return snapshot;

            pump(250);
        }

        return snapshot;
    }

    juce::MemoryBlock makePng(juce::Colour colour, int size)
    {
        juce::Image image(juce::Image::RGB, size, size, true);

        {
            juce::Graphics g(image);
            g.fillAll(colour);
            g.setColour(colour.contrasting());
            g.fillEllipse(0.0f, 0.0f, (float) size * 0.6f, (float) size * 0.6f);
        }

        juce::MemoryOutputStream out;
        juce::PNGImageFormat format;
        format.writeImageToStream(image, out);

        return { out.getData(), out.getDataSize() };
    }

    // A real MP3, tagged by the app's own writer - not a fixture invented
    // for the test.
    bool makeTrack(const juce::File& source, const juce::File& destination,
                    const juce::String& title, const juce::String& artist,
                    const juce::String& album, const juce::String& trackNumber,
                    const juce::MemoryBlock& artwork)
    {
        destination.deleteFile();

        if (! source.copyFileTo(destination))
        {
            std::cout << "  FAIL couldn't copy the test mp3 to "
                       << destination.getFullPathName() << std::endl;
            ++failures;
            return false;
        }

        using Field = inkwyrd::TagField;

        inkwyrd::TagChanges changes;
        changes.title = Field::setTo(title);
        changes.artist = Field::setTo(artist);
        changes.album = Field::setTo(album);
        changes.trackNumber = Field::setTo(trackNumber);

        if (artwork.getSize() > 0)
        {
            changes.artworkAction = inkwyrd::TagChanges::ArtworkAction::set;
            changes.artwork = artwork;
            changes.artworkMimeType = "image/png";
        }
        else
        {
            changes.artworkAction = inkwyrd::TagChanges::ArtworkAction::clear;
        }

        juce::String errorMessage;

        if (! inkwyrd::TagEditor::write(destination, changes, errorMessage))
        {
            std::cout << "  FAIL couldn't tag " << destination.getFileName()
                       << ": " << errorMessage << std::endl;
            ++failures;
            return false;
        }

        return true;
    }

    struct Blank : juce::Component
    {
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }
    };

#endif // JUCE_WINDOWS
}

int runSystemMediaControlsTest();

int runSystemMediaControlsTest()
{
#if ! JUCE_WINDOWS
    std::cout << "INKWYRD_MEDIATEST is Windows-only." << std::endl;
    return 1;
#else
    juce::ScopedJuceInitialiser_GUI juceInit;

    // INKWYRD_MEDIATEST=hold: publish one track and just sit there, so
    // another consumer entirely can be pointed at it - the overlay's own
    // reader, the volume popup's media panel, a media key. The automated
    // checks below read the session back through the consumer API, which
    // proves the data is there but not that a particular consumer can
    // make use of it.
    const auto holdSeconds = juce::SystemStats::getEnvironmentVariable("INKWYRD_MEDIATEST", "") == "hold"
                                 ? 45 : 0;

    // Whatever Windows reports as the session's owner is this exe's name,
    // which is how the read-back finds our own session among everyone
    // else's.
    const auto appId = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFileName();
    std::cout << "publishing as: " << appId << std::endl;

    auto source = juce::File::getCurrentWorkingDirectory()
                     .getChildFile("test-media/formats/song_mp3.mp3");

    if (! source.existsAsFile())
    {
        // Run from the build folder as well as from the repo root.
        source = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                     .getParentDirectory()
                     .getChildFile("../../../../../test-media/formats/song_mp3.mp3");
    }

    if (! source.existsAsFile())
    {
        std::cout << "FAIL couldn't find test-media/formats/song_mp3.mp3 - run from the repo root"
                   << std::endl;
        return 1;
    }

    auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getChildFile("inkwyrd-mediatest");
    folder.createDirectory();

    const auto artA = makePng(juce::Colours::rebeccapurple, 240);
    const auto artB = makePng(juce::Colours::orange, 120);

    auto trackA = folder.getChildFile("track-a.mp3");
    auto trackB = folder.getChildFile("track-b.mp3");
    auto trackC = folder.getChildFile("track-c-no-art.mp3");

    if (! makeTrack(source, trackA, "Caverns Below", "The Deep Choir", "Session One", "3", artA)
         || ! makeTrack(source, trackB, "Market Day", "Tabor and Pipe", "Session Two", "11", artB)
         || ! makeTrack(source, trackC, "Silent Vault", "No Cover Here", "Session Three", "1", {}))
        return 1;

    // The embedded art is not byte-identical to what went in - the tag
    // writer owns the picture frame - so what the read-back must match is
    // what is now IN the file.
    const auto embeddedA = (int) inkwyrd::TagEditor::read(trackA).artwork.getSize();
    const auto embeddedB = (int) inkwyrd::TagEditor::read(trackB).artwork.getSize();
    const auto embeddedC = (int) inkwyrd::TagEditor::read(trackC).artwork.getSize();

    std::cout << "embedded art: A=" << embeddedA << " B=" << embeddedB
               << " C=" << embeddedC << " bytes" << std::endl;

    check(embeddedA > 0, "track A has embedded art to publish");
    check(embeddedB > 0 && embeddedB != embeddedA, "track B has DIFFERENT embedded art");
    check(embeddedC == 0, "track C has no embedded art");

    SystemMediaControls media;

    if (holdSeconds > 0)
    {
        SystemMediaControls::Track held;
        held.file = trackA;
        held.title = "Caverns Below";
        held.artist = "The Deep Choir";
        held.album = "Session One";
        held.trackNumber = 3;

        media.setStatus(SystemMediaControls::Status::playing);
        media.setTrack(held);

        std::cout << "holding a published session for " << holdSeconds
                   << " seconds - point a consumer at it now" << std::endl;

        for (int second = 0; second < holdSeconds; ++second)
        {
            pump(1000);
            std::cout << "  " << (second + 1) << "s" << std::endl;
        }

        media.setStatus(SystemMediaControls::Status::closed);
        pump(500);
        folder.deleteRecursively();
        return 0;
    }

    // Nothing has been published yet, so nothing should exist for a
    // consumer to find. An app that is merely RUNNING must not appear in
    // the media panel: an empty entry of ours would win a consumer's
    // "nothing is playing, show me whatever there is" fallback.
    check(! readSession(appId).found, "no session exists before anything plays");

    std::cout << std::endl << "--- a track, with its artwork ---" << std::endl;

    SystemMediaControls::Track track;
    track.file = trackA;
    track.title = "Caverns Below";
    track.artist = "The Deep Choir";
    track.album = "Session One";
    track.trackNumber = 3;

    media.setStatus(SystemMediaControls::Status::playing);
    media.setTrack(track);
    pump(600);

    auto snapshot = readSessionWaitingForArt(appId, embeddedA);
    std::cout << "  (sessions for this app: " << snapshot.matching
               << ")" << std::endl;

    check(snapshot.found, "a consumer can see a session owned by this app at all");
    checkEquals(snapshot.title, "Caverns Below", "title reaches a consumer");
    checkEquals(snapshot.artist, "The Deep Choir", "artist reaches a consumer");
    checkEquals(snapshot.album, "Session One", "album reaches a consumer");
    check(snapshot.trackNumber == 3, "track number reaches a consumer (got "
                                       + juce::String(snapshot.trackNumber) + ")");

    // 4 == MediaPlaybackStatus::Playing. The overlay this was built for
    // picks the first session reporting exactly that, so it is the one
    // value that decides whether we get shown at all.
    check(snapshot.status == 4, "playback status reads as Playing (got "
                                  + juce::String(snapshot.status) + ")");

    check(snapshot.artBytes == embeddedA, "the album art arrives, whole (expected "
                                            + juce::String(embeddedA) + " bytes, got "
                                            + juce::String(snapshot.artBytes) + ")");
    check(snapshot.artIsRealImage, "the art is a real image, not stray bytes");
    std::cout << "  (art content type: " << snapshot.artContentType << ")" << std::endl;

    std::cout << std::endl << "--- changing track ---" << std::endl;

    track.file = trackB;
    track.title = "Market Day";
    track.artist = "Tabor and Pipe";
    track.album = "Session Two";
    track.trackNumber = 11;

    media.setTrack(track);
    pump(600);

    snapshot = readSessionWaitingForArt(appId, embeddedB);
    std::cout << "  (sessions for this app: " << snapshot.matching
               << ")" << std::endl;

    checkEquals(snapshot.title, "Market Day", "the new title replaces the old one");
    checkEquals(snapshot.artist, "Tabor and Pipe", "the new artist replaces the old one");
    check(snapshot.trackNumber == 11, "the new track number replaces the old one");
    check(snapshot.artBytes == embeddedB, "the NEW track's art replaces the old art (expected "
                                            + juce::String(embeddedB) + " bytes, got "
                                            + juce::String(snapshot.artBytes) + ")");

    std::cout << std::endl << "--- an app window's native handle being recreated ---" << std::endl;

    // The trap an earlier version of this DID fall into, kept as a
    // regression guard. JUCE destroys and recreates a window's HWND
    // whenever its desktop style flags change, which a skin change does
    // (DetachableWindow::applyThemeMetricsToAll). Binding the session to
    // an app window therefore left a GHOST session behind, still
    // reporting the old track as Playing - which is exactly the session a
    // consumer picks first. The class owns its own never-shown window
    // now, so this has to be a non-event: one session, still correct.
    auto window = std::make_unique<juce::DocumentWindow>("Inkwyrd media test",
                                                          juce::Colours::black,
                                                          juce::DocumentWindow::closeButton);
    window->setContentOwned(new Blank(), false);
    window->setBounds(40, 40, 320, 120);
    window->setVisible(true);
    pump(400);

    const auto handleBefore = window->getPeer() != nullptr
                                 ? window->getPeer()->getNativeHandle() : nullptr;
    window->setUsingNativeTitleBar(true);
    pump(400);

    auto* newPeer = window->getPeer();
    const auto handleAfter = newPeer != nullptr ? newPeer->getNativeHandle() : nullptr;

    // Without this the rest of the section would pass for the wrong
    // reason: an unchanged handle tests nothing.
    check(handleAfter != nullptr && handleAfter != handleBefore,
           "an app window's native handle really did change");

    window->setVisible(false);
    window = nullptr;
    pump(600);

    snapshot = readSessionWaitingForArt(appId, embeddedB);
    std::cout << "  (sessions for this app: " << snapshot.matching << ")" << std::endl;

    check(snapshot.matching == 1, "still exactly ONE session - no ghost (got "
                                    + juce::String(snapshot.matching) + ")");
    checkEquals(snapshot.title, "Market Day", "the track is still published correctly");
    check(snapshot.artBytes == embeddedB, "the art is still published correctly (got "
                                            + juce::String(snapshot.artBytes) + " bytes)");

    std::cout << std::endl << "--- a track with no embedded art ---" << std::endl;

    track.file = trackC;
    track.title = "Silent Vault";
    track.artist = "No Cover Here";
    track.album = "Session Three";
    track.trackNumber = 1;

    media.setTrack(track);

    // Long enough that late art WOULD have landed by now - the point of
    // this check is that the previous track's cover does not linger.
    pump(2500);
    snapshot = readSession(appId);
    std::cout << "  (sessions for this app: " << snapshot.matching << ")" << std::endl;

    checkEquals(snapshot.title, "Silent Vault", "a track with no art still publishes its title");
    check(snapshot.artBytes == 0, "the PREVIOUS track's art does not linger (got "
                                    + juce::String(snapshot.artBytes) + " bytes)");

    std::cout << std::endl << "--- pausing, then stopping ---" << std::endl;

    media.setStatus(SystemMediaControls::Status::paused);
    pump(600);
    snapshot = readSession(appId);

    // The consumer side has its OWN status enum, not the one the
    // publishing side sets: GlobalSystemMediaTransportControlsSession-
    // PlaybackStatus is Closed 0, Opened 1, Changing 2, Stopped 3,
    // Playing 4, Paused 5. Measured, because getting Playing wrong is
    // the difference between an overlay choosing us and ignoring us.
    check(snapshot.status == 5, "pausing reads as Paused (got " + juce::String(snapshot.status) + ")");

    media.setStatus(SystemMediaControls::Status::closed);
    pump(800);
    snapshot = readSession(appId);

    check(! snapshot.found, "stopping removes the session entirely (found="
                              + juce::String(snapshot.found ? 1 : 0) + " status="
                              + juce::String(snapshot.status) + ")");

    std::cout << std::endl << "--- playing again, and stopping, repeatedly ---" << std::endl;

    // Each stop destroys the session and its window, each play builds a
    // new one. If any of that leaked a session, this is where it would
    // pile up.
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        media.setStatus(SystemMediaControls::Status::playing);
        media.setTrack(track);
        pump(500);

        media.setStatus(SystemMediaControls::Status::closed);
        pump(500);
    }

    media.setStatus(SystemMediaControls::Status::playing);
    media.setTrack(track);
    pump(800);

    snapshot = readSession(appId);
    std::cout << "  (sessions for this app: " << snapshot.matching << ")" << std::endl;

    check(snapshot.found, "playing again after a stop publishes a session once more");
    check(snapshot.matching == 1, "three stop/play cycles leave exactly ONE session (got "
                                    + juce::String(snapshot.matching) + ")");
    checkEquals(snapshot.title, "Silent Vault", "the track is correct after the cycles");

    folder.deleteRecursively();

    std::cout << std::endl
               << (failures == 0 ? "MEDIA TEST PASSED" : "MEDIA TEST FAILED")
               << " (" << failures << " failure(s))" << std::endl;

    return failures == 0 ? 0 : 1;
#endif
}
