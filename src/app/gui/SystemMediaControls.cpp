#include "SystemMediaControls.h"

#if JUCE_WINDOWS

#include <windows.h>
#include <systemmediatransportcontrolsinterop.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>

// Linked here rather than in CMakeLists, same as dwmapi in
// DetachableWindow.cpp: the one file that needs it says so.
#pragma comment(lib, "WindowsApp.lib")

#include <atomic>
#include <memory>
#include <mutex>

#include <juce_events/juce_events.h>

#include "Log.h"
#include "TagEditor.h"

using namespace winrt::Windows::Media;
using namespace winrt::Windows::Storage::Streams;

namespace
{
    const wchar_t* const kWindowClassName = L"InkwyrdMediaSessionWindow";

    winrt::hstring toHstring(const juce::String& text)
    {
        return winrt::hstring(text.toWideCharPointer());
    }

    juce::String hexOf(const winrt::hresult_error& error)
    {
        return "0x" + juce::String::toHexString((int) error.code().value);
    }
}

struct SystemMediaControls::Impl : private juce::Thread
{
    explicit Impl(SystemMediaControls& ownerToUse)
        : juce::Thread("Inkwyrd media artwork"), owner(ownerToUse)
    {
        startThread();
    }

    ~Impl() override
    {
        // Order matters. Clearing this first means any artwork handoff
        // already queued on the message thread turns into a no-op rather
        // than touching a half-destroyed object. Both that handoff and
        // this destructor run on the message thread, so once this line
        // has run no queued one can still be waiting to find us alive.
        *alive = false;

        signalThreadShouldExit();
        workWaiter.signal();
        stopThread(2000);

        closeSession();
    }

    void setTrack(const Track& track)
    {
        if (track.file == juce::File())
        {
            // Already saying nothing. Worth the early-out because the
            // caller is a 500ms poll, and an app sitting idle would
            // otherwise redo all of this twice a second forever.
            if (! haveTrack && controls == nullptr)
                return;

            // Nothing playing. Said this way rather than as an empty
            // session - see Status::closed in the header.
            published = {};
            haveTrack = false;
            thumbnail = nullptr;
            ++artworkGeneration;
            closeSession();
            return;
        }

        const auto sameFile = haveTrack && track.file == published.file;

        const auto unchanged = sameFile
                                 && track.title == published.title
                                 && track.artist == published.artist
                                 && track.album == published.album
                                 && track.trackNumber == published.trackNumber;

        // `controls != nullptr` is not redundant. A stop tears the
        // session down but leaves this record of what was playing, so
        // pressing Play again on the very same track is "nothing
        // changed" as far as the fields go - and without this it would
        // silently never republish. Caught by INKWYRD_MEDIATEST's
        // stop/play cycles.
        if (unchanged && controls != nullptr)
            return;

        published = track;
        haveTrack = true;

        // Only for a genuinely different track. A reopened session can
        // reuse the stream reference it already has: that stream belongs
        // to this process, not to the session that was torn down, so the
        // art comes straight back rather than being re-read off disk.
        if (! sameFile)
        {
            // The art belongs to the track that's leaving. Bumping the
            // generation is what stops a slow artwork read for the
            // PREVIOUS track landing on this one a moment from now.
            thumbnail = nullptr;
            ++artworkGeneration;
            requestArtwork(track.file, artworkGeneration);
        }

        if (! openSessionIfNeeded())
            return;

        pushToSession();
    }

    void setStatus(Status newStatus)
    {
        if (newStatus == status)
            return;

        status = newStatus;

        if (status == Status::closed)
        {
            closeSession();
            return;
        }

        applyStatus();
    }

private:
    // Creates the session, and the window it belongs to, the first time
    // there's actually something to publish. Both live until there isn't.
    bool openSessionIfNeeded()
    {
        if (controls != nullptr)
            return true;

        if (! createWindow())
            return false;

        try
        {
            // The whole reason a desktop app can do this at all: the
            // session is fetched FOR A WINDOW, rather than activated the
            // way a packaged app would.
            auto interop = winrt::get_activation_factory<SystemMediaTransportControls,
                                                          ISystemMediaTransportControlsInterop>();

            winrt::check_hresult(interop->GetForWindow(sessionWindow,
                                                        winrt::guid_of<SystemMediaTransportControls>(),
                                                        winrt::put_abi(controls)));
        }
        catch (const winrt::hresult_error& error)
        {
            // Deliberately loud. A silent failure here looks exactly like
            // "this app doesn't support now-playing", which is a bug
            // report nobody can act on.
            logLine("[Media] couldn't open the Windows media session: " + hexOf(error));
            controls = nullptr;
            destroyWindow();
            return false;
        }

        controls.IsEnabled(true);
        controls.IsPlayEnabled(true);
        controls.IsPauseEnabled(true);
        controls.IsStopEnabled(true);
        controls.IsNextEnabled(true);

        // No previous-track operation exists in PlaylistEngine, so the
        // button isn't offered rather than offered and ignored.
        controls.IsPreviousEnabled(false);

        installButtonHandler();

        logLine("[Media] publishing to the Windows media controls");
        return true;
    }

    // Our own window, never shown, so the handle can't be recreated
    // underneath the session - see the header for the ghost-session
    // measurement that made this necessary.
    bool createWindow()
    {
        if (sessionWindow != nullptr)
            return true;

        auto* instance = (HINSTANCE) GetModuleHandleW(nullptr);

        if (! windowClassRegistered)
        {
            WNDCLASSW windowClass {};
            windowClass.lpfnWndProc = DefWindowProcW;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kWindowClassName;

            if (RegisterClassW(&windowClass) == 0
                 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                logLine("[Media] couldn't register the media session window class: "
                         + juce::String((int) GetLastError()));
                return false;
            }

            windowClassRegistered = true;
        }

        // WS_EX_TOOLWINDOW as well as never calling ShowWindow: belt and
        // braces against this ever turning up in alt-tab or on the
        // taskbar if something else decides to show it.
        sessionWindow = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClassName,
                                         L"Inkwyrd Audio media session",
                                         WS_OVERLAPPED, 0, 0, 0, 0,
                                         nullptr, nullptr, instance, nullptr);

        if (sessionWindow == nullptr)
        {
            logLine("[Media] couldn't create the media session window: "
                     + juce::String((int) GetLastError()));
            return false;
        }

        return true;
    }

    void destroyWindow()
    {
        if (sessionWindow == nullptr)
            return;

        DestroyWindow(sessionWindow);
        sessionWindow = nullptr;
    }

    void installButtonHandler()
    {
        // `alive` by value, `owner` by pointer: the pointer is only ever
        // dereferenced after the flag has been checked on the message
        // thread, and the flag is cleared before this object (a member of
        // that same owner) is destroyed.
        buttonToken = controls.ButtonPressed(
            [flag = alive, target = &owner](auto&&,
                 const SystemMediaTransportControlsButtonPressedEventArgs& args)
            {
                // WinRT delivers this on a thread pool thread, and
                // everything it leads to - the transport buttons, the
                // engine - is message-thread-only.
                const auto button = args.Button();

                juce::MessageManager::callAsync([flag, target, button]
                {
                    if (! *flag)
                        return;

                    switch (button)
                    {
                        case SystemMediaTransportControlsButton::Play:
                        case SystemMediaTransportControlsButton::Pause:
                            if (target->onPlayPause)
                                target->onPlayPause();
                            break;

                        case SystemMediaTransportControlsButton::Stop:
                            if (target->onStop)
                                target->onStop();
                            break;

                        case SystemMediaTransportControlsButton::Next:
                            if (target->onNext)
                                target->onNext();
                            break;

                        default:
                            break;
                    }
                });
            });
    }

    // Takes the session away properly: cleared, closed, released, and
    // only THEN is the window destroyed. Doing it in that order is the
    // difference between vanishing cleanly and leaving a ghost session
    // reporting a track forever.
    void closeSession()
    {
        if (controls == nullptr)
        {
            destroyWindow();
            return;
        }

        try
        {
            if (buttonToken.value != 0)
                controls.ButtonPressed(buttonToken);

            auto updater = controls.DisplayUpdater();
            updater.ClearAll();
            updater.Update();

            controls.PlaybackStatus(MediaPlaybackStatus::Closed);
            controls.IsEnabled(false);
        }
        catch (const winrt::hresult_error& error)
        {
            logLine("[Media] couldn't close the media session cleanly: " + hexOf(error));
        }

        buttonToken = {};
        controls = nullptr;

        destroyWindow();
    }

    void pushToSession()
    {
        if (controls == nullptr || ! haveTrack)
            return;

        try
        {
            auto updater = controls.DisplayUpdater();
            updater.Type(MediaPlaybackType::Music);

            auto music = updater.MusicProperties();
            music.Title(toHstring(published.title));
            music.Artist(toHstring(published.artist));
            music.AlbumTitle(toHstring(published.album));
            music.TrackNumber((uint32_t) juce::jmax(0, published.trackNumber));

            // May be null, which is the right value for "art hasn't been
            // read yet" - it arrives in its own update shortly after.
            updater.Thumbnail(thumbnail);

            updater.Update();
            applyStatus();
        }
        catch (const winrt::hresult_error& error)
        {
            logLine("[Media] couldn't publish the current track: " + hexOf(error));
        }
    }

    void applyStatus()
    {
        if (controls == nullptr)
            return;

        try
        {
            switch (status)
            {
                case Status::playing: controls.PlaybackStatus(MediaPlaybackStatus::Playing); break;
                case Status::paused:  controls.PlaybackStatus(MediaPlaybackStatus::Paused);  break;
                case Status::closed:  controls.PlaybackStatus(MediaPlaybackStatus::Closed);  break;
            }
        }
        catch (const winrt::hresult_error& error)
        {
            logLine("[Media] couldn't set the playback status: " + hexOf(error));
        }
    }

    void requestArtwork(const juce::File& file, int generation)
    {
        {
            const std::lock_guard<std::mutex> lock(requestLock);
            requestedFile = file;
            requestedGeneration = generation;
            requestPending = true;
        }

        workWaiter.signal();
    }

    // Message thread, via callAsync from the worker.
    void artworkArrived(const RandomAccessStreamReference& reference, int generation)
    {
        if (generation != artworkGeneration)
            return; // read for a track that has since been replaced

        thumbnail = reference;

        if (controls == nullptr || ! haveTrack)
            return;

        try
        {
            auto updater = controls.DisplayUpdater();
            updater.Thumbnail(thumbnail);
            updater.Update();
        }
        catch (const winrt::hresult_error& error)
        {
            logLine("[Media] couldn't publish the album art: " + hexOf(error));
        }
    }

    void run() override
    {
        // This thread exists for two reasons that both come back to the
        // same place: reading a file's tags is disk I/O, and turning the
        // art into a WinRT stream needs async calls that must not be
        // waited on from the message thread.
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        }
        catch (const winrt::hresult_error& error)
        {
            logLine("[Media] artwork thread couldn't start WinRT: " + hexOf(error));
            return;
        }

        while (! threadShouldExit())
        {
            juce::File file;
            int generation = 0;
            auto have = false;

            {
                const std::lock_guard<std::mutex> lock(requestLock);

                if (requestPending)
                {
                    file = requestedFile;
                    generation = requestedGeneration;
                    requestPending = false;
                    have = true;
                }
            }

            if (! have)
            {
                workWaiter.wait(-1);
                continue;
            }

            buildArtwork(file, generation);
        }

        winrt::uninit_apartment();
    }

    void buildArtwork(const juce::File& file, int generation)
    {
        auto tags = inkwyrd::TagEditor::read(file);

        if (tags.artwork.getSize() == 0)
            return; // no embedded art; the session keeps its null thumbnail

        RandomAccessStreamReference reference { nullptr };

        try
        {
            // An in-memory stream rather than a temp file on disk: there
            // is then nothing to clean up, and nothing for a consumer to
            // be halfway through reading when the next track replaces it.
            InMemoryRandomAccessStream stream;
            DataWriter writer(stream);

            const auto* bytes = static_cast<const uint8_t*>(tags.artwork.getData());
            writer.WriteBytes(winrt::array_view<const uint8_t>(bytes, bytes + tags.artwork.getSize()));

            writer.StoreAsync().get();
            writer.FlushAsync().get();

            // Leaves the stream open now the bytes are in it - without
            // this the writer closes it on the way out and the reference
            // below has nothing to read.
            writer.DetachStream();
            stream.Seek(0);

            reference = RandomAccessStreamReference::CreateFromStream(stream);
        }
        catch (const winrt::hresult_error& error)
        {
            logLine("[Media] couldn't prepare the album art: " + hexOf(error));
            return;
        }

        juce::MessageManager::callAsync([flag = alive, target = this, reference, generation]
        {
            if (! *flag)
                return;

            target->artworkArrived(reference, generation);
        });
    }

    SystemMediaControls& owner;

    // Checked on the message thread before anything above is touched.
    // Shared so a queued handoff holds it alive even once we don't.
    std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>>(true) };

    SystemMediaTransportControls controls { nullptr };
    winrt::event_token buttonToken {};

    HWND sessionWindow = nullptr;
    bool windowClassRegistered = false;

    Track published;
    bool haveTrack = false;
    Status status = Status::closed;
    RandomAccessStreamReference thumbnail { nullptr };

    // Message thread only. Incremented on every real track change, so a
    // late artwork read can tell it's late.
    int artworkGeneration = 0;

    std::mutex requestLock;
    juce::File requestedFile;
    int requestedGeneration = 0;
    bool requestPending = false;
    juce::WaitableEvent workWaiter;
};

SystemMediaControls::SystemMediaControls()
    : impl(std::make_unique<Impl>(*this))
{
}

SystemMediaControls::~SystemMediaControls() = default;

void SystemMediaControls::setTrack(const Track& track)
{
    impl->setTrack(track);
}

void SystemMediaControls::setStatus(Status status)
{
    impl->setStatus(status);
}

#else

// Nothing else in the app is built anywhere but Windows, but this keeps
// the call sites free of platform conditionals.
struct SystemMediaControls::Impl {};

SystemMediaControls::SystemMediaControls() = default;
SystemMediaControls::~SystemMediaControls() = default;
void SystemMediaControls::setTrack(const Track&) {}
void SystemMediaControls::setStatus(Status) {}

#endif
