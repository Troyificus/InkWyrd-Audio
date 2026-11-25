#pragma once

#include <functional>
#include <memory>

#include <juce_core/juce_core.h>

// Publishes what's playing to Windows' System Media Transport Controls:
// the media panel in the volume popup, the keyboard's media keys, and
// anything that reads GlobalSystemMediaTransportControlsSessionManager -
// OBS/Streamlabs now-playing overlays, Rainmeter, and the rest.
//
// WHY SMTC AND NOT A PRIVATE FEED TO ONE OVERLAY. The request this was
// built for was a specific Streamlabs overlay that couldn't see Inkwyrd.
// That overlay reads SMTC, as every other now-playing tool on Windows
// does, so publishing here reaches all of them at once and neither side
// has to know the other exists. The alternative considered - broadcasting
// now-playing JSON over ControlServer's existing websocket - would have
// needed matching work in the overlay and would only ever have fed that
// one consumer. It's also what gives media-key control for free, which is
// how Winamp-era players had it.
//
// WHAT WAS MEASURED BEFORE ANY OF THIS WAS BUILT, in a standalone Win32
// spike and then in INKWYRD_MEDIATEST (which publishes with this class
// and reads it back through the consumer API), so none of it gets
// re-guessed:
//
//  * A plain Win32 app CAN own an SMTC session - this is not UWP-only.
//    The session comes from ISystemMediaTransportControlsInterop::
//    GetForWindow(hwnd) and a consumer then sees the title/artist/album
//    with the app's own exe name as the session's SourceAppUserModelId.
//
//  * Album art MUST be a stream reference. Setting the thumbnail from a
//    file:/// Uri silently produces NO thumbnail for consumers - not an
//    error, just a null reference on the far side, which looks exactly
//    like "this app publishes no art". CreateFromStream (an
//    InMemoryRandomAccessStream) and CreateFromFile (a StorageFile) both
//    genuinely work; this uses the first, so there's no temp file to go
//    stale or be rewritten while a consumer is reading it.
//
//  * Building that stream needs an async WinRT call, which must not run
//    on the message thread, so artwork is prepared on a worker thread and
//    attached when it's ready. Text fields need no async at all and are
//    published immediately - so a track's name appears at once and its
//    art a moment later, which is how other players behave too (the
//    overlay this was built for even retries its art read for that exact
//    reason).
//
//  * THE SESSION WINDOW HAS TO BE OURS. The obvious implementation binds
//    the session to the Player window, and that was built and measured
//    first. It leaves a GHOST SESSION behind: JUCE destroys and recreates
//    a window's HWND whenever its desktop style flags change (a skin
//    change does exactly that, via DetachableWindow::
//    applyThemeMetricsToAll), and the session belonging to the dead
//    handle does NOT go away with it. Measured: two sessions for this app
//    at once, the stale one still reporting the track that was playing at
//    the moment of the skin change, still saying Playing. Since the
//    overlay picks the FIRST session reporting Playing, it can pick the
//    ghost - and then freeze on a track that stopped long ago. Closing
//    the old session on the way past doesn't help, because by the time
//    anything notices the handle changed, the window it belonged to is
//    already gone.
//    So this class creates its OWN top-level window, never shown, and
//    keeps it for as long as the session lives. That handle cannot change
//    underneath it, which makes the whole class of problem go away rather
//    than needing to be caught. A never-shown window owns a fully working
//    session: title, artist, album, track number and artwork all verified
//    through the consumer API.
//    The cost, accepted deliberately: the media panel's entry isn't tied
//    to a window the user can see, so clicking it won't bring the Player
//    forward.
//
// No WinRT type appears in this header on purpose: winrt/base.h and
// JUCE's own windows.h handling only need to meet in the .cpp.
class SystemMediaControls
{
public:
    SystemMediaControls();
    ~SystemMediaControls();

    struct Track
    {
        juce::String title, artist, album;
        int trackNumber = 0;

        // Where the artwork is read from, on the worker thread. An empty
        // file means "nothing playing", and tears the session down.
        juce::File file;
    };

    // closed means "there is nothing to show", and tears the session
    // down rather than leaving it reporting an empty track: a consumer
    // that finds nothing playing falls back to whatever session it can
    // find, and an empty entry of ours would win that fallback and show
    // the viewer a blank now-playing panel.
    enum class Status { playing, paused, closed };

    // Both of these are cheap, idempotent and meant to be called from a
    // poll: each does nothing at all when what it's told matches what's
    // already published.
    //
    // Call setStatus FIRST when both change together. The session is
    // created lazily by the first real track, and it then opens already
    // reporting the right status rather than passing through "stopped" on
    // the way - which a consumer polling at the wrong moment would see.
    void setStatus(Status status);
    void setTrack(const Track& track);

    // Pressed in the Windows media panel, or on a keyboard's media keys.
    // Called on the message thread. Only ever fires for the buttons this
    // actually enables - there is deliberately no Previous, because
    // PlaylistEngine has no previous-track operation to map it to, and a
    // button that does nothing is worse than one that isn't offered.
    std::function<void()> onPlayPause, onNext, onStop;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SystemMediaControls)
};
