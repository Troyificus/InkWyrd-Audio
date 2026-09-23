#include "DetachableWindow.h"

#include "InkwyrdTheme.h"
#include "Log.h"
#include "WindowLayoutStore.h"
#include "WindowSnapping.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <commctrl.h>
#endif

juce::Array<DetachableWindow*> DetachableWindow::activeWindows;
bool DetachableWindow::groupMoveInProgress = false;

namespace
{
    // How long after the last move/resize before the layout is written
    // to disk. A drag fires moved() many times a second; this coalesces
    // a whole gesture into one write shortly after it settles.
    constexpr int kSaveDebounceMs = 400;

    // How close an edge has to get before it snaps. Deliberately
    // generous: the snap now happens live, mid-drag, so this is the
    // distance at which a window visibly pulls itself into place -
    // "strong magnet" territory rather than a quiet correction.
    constexpr int kSnapThreshold = 24;

    // Looser than a snapped-flush 0px when deciding what counts as
    // "already docked, come along with me", since a window placed by
    // hand can be a pixel or two out.
    constexpr int kDockTolerance = 4;

    // Small enough never to get in the way of a deliberate resize, big
    // enough that a window always has a title bar left to grab.
    constexpr int kMinimumWindowWidth = 220;
    constexpr int kMinimumWindowHeight = 120;

#if JUCE_WINDOWS
    constexpr UINT_PTR kSubclassId = 1;

    // How many times Windows asked a window to erase its background during
    // the current drag or resize - logged when it ends. Tells whether the
    // dark fill below is even on the path the white "ghost" comes from.
    int erasesThisGesture = 0;

    juce::Rectangle<int> toRectangle(const RECT& r)
    {
        return juce::Rectangle<int>::leftTopRightBottom(r.left, r.top, r.right, r.bottom);
    }

    void writeInto(RECT& r, juce::Rectangle<int> bounds)
    {
        r.left = bounds.getX();
        r.top = bounds.getY();
        r.right = bounds.getRight();
        r.bottom = bounds.getBottom();
    }

    // The work area (screen minus taskbar) of whichever monitor the
    // proposed rectangle is mostly on, in physical pixels.
    juce::Rectangle<int> workAreaFor(const RECT& proposed)
    {
        auto* monitor = MonitorFromRect(&proposed, MONITOR_DEFAULTTONEAREST);

        MONITORINFO info {};
        info.cbSize = sizeof(info);

        if (monitor != nullptr && GetMonitorInfo(monitor, &info))
            return toRectangle(info.rcWork);

        return WindowLayoutStore::primaryDisplayArea();
    }
#endif
}

#if JUCE_WINDOWS
// Handles the messages Windows sends WHILE a window is being dragged or
// resized, which is the only point at which the position can still be
// changed - see the header. The adjusted rectangle is passed on to
// JUCE's own handler rather than swallowed, so JUCE's size limits and
// bookkeeping still run on top of it.
static LRESULT CALLBACK detachableWindowSubclassProc(HWND hwnd, UINT message,
                                                      WPARAM wParam, LPARAM lParam,
                                                      UINT_PTR, DWORD_PTR refData)
{
    if (auto* window = reinterpret_cast<DetachableWindow*>(refData))
    {
        switch (message)
        {
            case WM_ENTERSIZEMOVE:
                window->beginNativeDragFromHook();
                break;

            case WM_EXITSIZEMOVE:
                window->endNativeDragFromHook();
                logLine("[Window] resize/move of \"" + window->getWindowId() + "\" ended: "
                         + juce::String(erasesThisGesture) + " background erase(s) filled dark");
                erasesThisGesture = 0;
                break;

            // Growing a window exposes a strip nobody has drawn yet. JUCE
            // answers this with "done" WITHOUT drawing anything, so that
            // strip shows white until the next paint lands a frame later -
            // the white "ghost" on every resize, in every renderer, since
            // beta.10 at least. Windows sends this synchronously as part of
            // the resize itself, before any paint, so filling it with the
            // window's own background colour here turns the ghost dark.
            // The DC is clipped to the newly exposed area, so nothing
            // already drawn is covered.
            case WM_ERASEBKGND:
            {
                RECT client {};
                GetClientRect(hwnd, &client);
                auto colour = inkwyrd::theme::panel;
                if (auto brush = CreateSolidBrush(RGB(colour.getRed(), colour.getGreen(), colour.getBlue())))
                {
                    FillRect(reinterpret_cast<HDC>(wParam), &client, brush);
                    DeleteObject(brush);
                }

                ++erasesThisGesture;
                return 1;
            }

            // These two are ANSWERED here, not passed on. Letting JUCE
            // also process them re-runs its own physical<->logical border
            // round-trip on the rectangle this hook just adjusted, and
            // the small error that introduces accumulates across the
            // hundreds of messages one drag produces - measured: a window
            // dragged 142px left ended up 377px to the RIGHT and pinned
            // to the top of the screen. Both messages only ever mean "you
            // may adjust this before it happens"; the move that actually
            // results still reaches JUCE as WM_WINDOWPOSCHANGED.
            case WM_MOVING:
                window->applyMoveSnapFromHook(reinterpret_cast<RECT*>(lParam));
                return TRUE;

            case WM_SIZING:
                window->applyResizeSnapFromHook(reinterpret_cast<RECT*>(lParam), (int) wParam);
                return TRUE;

            default:
                break;
        }
    }

    return DefSubclassProc(hwnd, message, wParam, lParam);
}
#endif

DetachableWindow::DetachableWindow(const juce::String& windowName, juce::String windowIdToUse,
                                    juce::String subtitle,
                                    AppSettings& settingsToUse, juce::Rectangle<int> defaultBounds,
                                    bool defaultVisible,
                                    int titleBarButtons)
    : DocumentWindow(windowName, inkwyrd::theme::panel, titleBarButtons),
      windowId(std::move(windowIdToUse)),
      titleBarSubtitle(std::move(subtitle)),
      settings(settingsToUse),
      restoredVisible(defaultVisible)
{
    activeWindows.add(this);

    // The theme's title bar carries a logo and two lines of text, which
    // cannot be drawn on a native Windows caption - the OS only exposes
    // its colour. Turning it off does NOT move dragging into JUCE's
    // hands; see the header.
    setUsingNativeTitleBar(false);
    setTitleBarHeight(inkwyrd::theme::titleBarHeight);

    setResizable(true, true);

    auto saved = WindowLayoutStore::fromJson(settings.getWindowLayoutJson());
    auto it = saved.find(windowId);

    auto bounds = defaultBounds;
    if (it != saved.end())
    {
        bounds = WindowLayoutStore::clampToNearestDisplay(it->second.bounds);
        restoredVisible = it->second.visible;
    }

    setBounds(bounds);
    lastMovedPosition = bounds.getPosition();
}

DetachableWindow::~DetachableWindow()
{
    stopTimer();
    removeNativeHook();
    activeWindows.removeFirstMatchingValue(this);
}

void DetachableWindow::installNativeHookIfNeeded()
{
#if JUCE_WINDOWS
    auto* handle = getWindowHandle();

    if (handle == nullptr || handle == hookedWindowHandle)
        return;

    // The peer can be recreated (JUCE does this for some style changes),
    // which leaves the old subclass attached to a dead HWND.
    removeNativeHook();

    if (SetWindowSubclass((HWND) handle, detachableWindowSubclassProc, kSubclassId,
                           reinterpret_cast<DWORD_PTR>(this)))
        hookedWindowHandle = handle;
#endif
}

void DetachableWindow::removeNativeHook()
{
#if JUCE_WINDOWS
    if (hookedWindowHandle != nullptr)
    {
        RemoveWindowSubclass((HWND) hookedWindowHandle, detachableWindowSubclassProc, kSubclassId);
        hookedWindowHandle = nullptr;
    }
#endif
}

juce::Array<juce::Rectangle<int>> DetachableWindow::physicalObstacles() const
{
    juce::Array<juce::Rectangle<int>> obstacles;

#if JUCE_WINDOWS
    for (auto* other : otherLiveWindows())
    {
        if (isCarrying(other))
            continue;

        if (auto* handle = other->getWindowHandle())
        {
            RECT r {};
            if (GetWindowRect((HWND) handle, &r))
                obstacles.add(toRectangle(r));
        }
    }
#endif

    return obstacles;
}

void DetachableWindow::beginNativeDragFromHook()
{
    nativeDragActive = true;
    lastMovedPosition = getBounds().getPosition();

#if JUCE_WINDOWS
    // Where the window and the cursor both were when this gesture
    // started. Everything during the drag is derived from these rather
    // than from the window's current position - see applyMoveSnapFromHook
    // for why that distinction is the difference between a magnet and a
    // trap.
    POINT cursor {};
    RECT startRect {};

    dragStartValid = GetCursorPos(&cursor)
                      && getWindowHandle() != nullptr
                      && GetWindowRect((HWND) getWindowHandle(), &startRect);

    if (dragStartValid)
    {
        dragStartCursorX = cursor.x;
        dragStartCursorY = cursor.y;
        dragStartBounds = toRectangle(startRect);
    }
#endif

    // Captured here, before anything has moved, so the current bounds
    // ARE the pre-drag bounds - no need to reconstruct them afterwards.
    if (carriesDockedWindows())
        captureDockedGroup(getBounds());
    else
        dockedGroup.clear();
}

void DetachableWindow::endNativeDragFromHook()
{
    nativeDragActive = false;
    dockedGroup.clear();
    persistNow();
}

void DetachableWindow::applyMoveSnapFromHook(void* rectPointer)
{
#if JUCE_WINDOWS
    auto* proposed = static_cast<RECT*>(rectPointer);
    if (proposed == nullptr)
        return;

    // The proposed rectangle is NOT used for the position. Windows works
    // out each proposal from where the window currently IS plus the
    // mouse movement since the last message, so snapping the proposal
    // feeds the snap back into its own input: every message proposes a
    // few pixels away from the snapped position, that's still inside the
    // threshold, and it gets pulled straight back. The window sticks to
    // whatever it first touched and cannot be dragged off it at all -
    // measured, a window glued to a neighbour's top edge ignored drags
    // of 200px.
    //
    // So the true position is rebuilt from the CURSOR instead, which
    // moves independently of anything we do to the window. Snapping that
    // is a magnet you can always pull away from.
    auto candidate = toRectangle(*proposed);

    if (dragStartValid)
    {
        POINT cursor {};
        if (GetCursorPos(&cursor))
            candidate = dragStartBounds.translated(cursor.x - dragStartCursorX,
                                                    cursor.y - dragStartCursorY);
    }

    auto snapped = inkwyrd::snapRectangle(candidate, physicalObstacles(),
                                           workAreaFor(*proposed), kSnapThreshold);

    writeInto(*proposed, snapped);
#else
    juce::ignoreUnused(rectPointer);
#endif
}

void DetachableWindow::applyResizeSnapFromHook(void* rectPointer, int edge)
{
#if JUCE_WINDOWS
    auto* proposed = static_cast<RECT*>(rectPointer);
    if (proposed == nullptr)
        return;

    const auto stretchingLeft = edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT;
    const auto stretchingRight = edge == WMSZ_RIGHT || edge == WMSZ_TOPRIGHT || edge == WMSZ_BOTTOMRIGHT;
    const auto stretchingTop = edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT;
    const auto stretchingBottom = edge == WMSZ_BOTTOM || edge == WMSZ_BOTTOMLEFT || edge == WMSZ_BOTTOMRIGHT;

    // Same reasoning as the move: rebuild the dragged edges from the
    // cursor so a snapped edge can still be pulled away from, rather
    // than re-snapping our own previous output forever.
    auto candidate = toRectangle(*proposed);

    if (dragStartValid)
    {
        POINT cursor {};
        if (GetCursorPos(&cursor))
        {
            auto deltaX = cursor.x - dragStartCursorX;
            auto deltaY = cursor.y - dragStartCursorY;

            auto left = stretchingLeft ? dragStartBounds.getX() + deltaX : candidate.getX();
            auto right = stretchingRight ? dragStartBounds.getRight() + deltaX : candidate.getRight();
            auto top = stretchingTop ? dragStartBounds.getY() + deltaY : candidate.getY();
            auto bottom = stretchingBottom ? dragStartBounds.getBottom() + deltaY : candidate.getBottom();

            if (right > left && bottom > top)
                candidate = juce::Rectangle<int>::leftTopRightBottom(left, top, right, bottom);
        }
    }

    auto snapped = inkwyrd::snapResizedEdges(candidate, physicalObstacles(),
                                              workAreaFor(*proposed), kSnapThreshold,
                                              stretchingLeft, stretchingRight,
                                              stretchingTop, stretchingBottom);

    // This hook answers WM_SIZING itself, so JUCE's own constrainer no
    // longer gets to enforce a floor - without this a window could be
    // dragged down to nothing and become impossible to grab again.
    if (snapped.getWidth() < kMinimumWindowWidth || snapped.getHeight() < kMinimumWindowHeight)
        return;

    writeInto(*proposed, snapped);
#else
    juce::ignoreUnused(rectPointer, edge);
#endif
}

juce::Array<DetachableWindow*> DetachableWindow::otherLiveWindows() const
{
    juce::Array<DetachableWindow*> result;

    for (auto* window : activeWindows)
        if (window != this && window->isVisible() && ! window->isMinimised())
            result.add(window);

    return result;
}

bool DetachableWindow::isCarrying(const DetachableWindow* window) const
{
    for (auto& member : dockedGroup)
        if (member.getComponent() == window)
            return true;

    return false;
}

void DetachableWindow::captureDockedGroup(juce::Rectangle<int> boundsToTestFrom)
{
    dockedGroup.clear();

    auto others = otherLiveWindows();

    juce::Array<juce::Rectangle<int>> rectangles;
    rectangles.add(boundsToTestFrom); // index 0 is always this window
    for (auto* other : others)
        rectangles.add(other->getBounds());

    for (auto index : inkwyrd::findDockedGroup(rectangles, 0, kDockTolerance))
        dockedGroup.add(others[index - 1]); // -1: index 0 was this window
}

void DetachableWindow::translateDockedGroup(juce::Point<int> delta)
{
    if (delta.isOrigin() || dockedGroup.isEmpty())
        return;

    // THE re-entrancy guard, and it is not optional. Moving a companion
    // fires that companion's own moved(), which without this treats
    // ITSELF as a drag leader and moves the window that just moved it
    // straight back - a feedback loop that runs a docked pair off the
    // screen and recurses until the stack gives out (a real
    // STATUS_FATAL_USER_CALLBACK_EXCEPTION, reported from beta.11).
    const juce::ScopedValueSetter<bool> scope(groupMoveInProgress, true);

    for (auto& member : dockedGroup)
        if (auto* window = member.getComponent())
            window->setBounds(window->getBounds().translated(delta.x, delta.y));
}

void DetachableWindow::applyOwnershipIfNeeded()
{
#if JUCE_WINDOWS
    if (isMasterWindow())
        return;

    DetachableWindow* master = nullptr;
    for (auto* window : activeWindows)
        if (window->isMasterWindow())
            master = window;

    if (master == nullptr || master == this)
        return;

    auto* peer = getPeer();
    auto* masterPeer = master->getPeer();
    if (peer == nullptr || masterPeer == nullptr)
        return;

    auto handle = (HWND) peer->getNativeHandle();
    auto ownerHandle = (HWND) masterPeer->getNativeHandle();
    if (handle == nullptr || ownerHandle == nullptr)
        return;

    // GWLP_HWNDPARENT on a top-level window is its OWNER, not its
    // parent - a genuinely confusing piece of Win32 naming. Re-checked
    // every time rather than tracked with a flag, because a peer can be
    // recreated underneath us and would come back unowned.
    if ((HWND) GetWindowLongPtr(handle, GWLP_HWNDPARENT) == ownerHandle)
        return;

    SetWindowLongPtr(handle, GWLP_HWNDPARENT, (LONG_PTR) ownerHandle);
#endif
}

void DetachableWindow::applyThemeMetricsToAll()
{
    // Title bar height is applied per window when it is built, so a skin
    // that changes it has to reach every window that already exists.
    // Everything else in the palette is read at paint time and needs only
    // a repaint.
    for (auto* window : activeWindows)
    {
        window->setTitleBarHeight(inkwyrd::theme::titleBarHeight);
        window->sendLookAndFeelChange();
        window->resized();
        window->repaint();
    }
}

void DetachableWindow::applyOwnershipToAll()
{
    for (auto* window : activeWindows)
        window->applyOwnershipIfNeeded();
}

void DetachableWindow::setHiddenByMasterMinimise(bool shouldBeHidden)
{
    hiddenByMasterMinimise = shouldBeHidden;
    setVisible(! shouldBeHidden);

    // Owned windows come back where Windows left them in the stack, not
    // necessarily above whatever the user has been using in the
    // meantime. Being explicit costs nothing and is the difference
    // between "they all came back" and "most of them did".
    if (! shouldBeHidden)
        toFront(false); // false: don't steal keyboard focus from the master
}

void DetachableWindow::applyRendererIfNeeded()
{
    // Why this exists: dragging a window's edge showed a white band over
    // the newly exposed area, and the content lagged at the old size -
    // in beta.30 too, so not the sprites. JUCE 8's Direct2D renderer
    // doesn't paint in WM_PAINT; it queues the area and paints on the
    // next vblank callback, which lags during Windows' modal resize loop.
    // The software renderer paints synchronously inside WM_PAINT.
    //
    // A switch rather than a new default until someone has compared the
    // two by dragging a real window - a synthetic drag can't reproduce
    // Windows' own resize loop faithfully, and this can't be judged from
    // a screenshot.
    auto requested = juce::SystemStats::getEnvironmentVariable("INKWYRD_RENDERER", "").trim().toLowerCase();
    if (requested.isEmpty())
        return;

    auto* peer = getPeer();
    if (peer == nullptr)
        return;

    auto engineName = requested == "software" ? "Software Renderer" : "Direct2D";
    auto index = peer->getAvailableRenderingEngines().indexOf(engineName);

    if (index >= 0 && peer->getCurrentRenderingEngine() != index)
        peer->setCurrentRenderingEngine(index);
}

void DetachableWindow::closeButtonPressed()
{
    setVisible(false);
}

void DetachableWindow::moved()
{
    DocumentWindow::moved();

    auto position = getBounds().getPosition();

    // The snap itself happens in the native hook, before the window
    // moves. All that's left here is carrying the docked group along by
    // however far this window actually travelled.
    if (nativeDragActive && ! groupMoveInProgress)
        translateDockedGroup(position - lastMovedPosition);

    lastMovedPosition = position;
    startTimer(kSaveDebounceMs);
}

void DetachableWindow::resized()
{
    DocumentWindow::resized();
    startTimer(kSaveDebounceMs);
}

void DetachableWindow::visibilityChanged()
{
    DocumentWindow::visibilityChanged();

    // The native window only exists once this is shown, so this is the
    // earliest the hook can be attached - and it has to be re-checked
    // every time, in case the peer was recreated.
    if (isVisible())
    {
        installNativeHookIfNeeded();
        applyRendererIfNeeded();

        // Same reasoning as the hook: the native window only exists once
        // shown, so a satellite reopened later has to be re-owned here
        // rather than only at startup.
        applyOwnershipIfNeeded();
    }

    // Not debounced like moved()/resized() - a show/hide toggle is a
    // single deliberate click, not a continuous stream of events.
    persistNow();
}

void DetachableWindow::timerCallback()
{
    stopTimer();
    persistNow();
}

void DetachableWindow::persistNow()
{
    auto states = WindowLayoutStore::fromJson(settings.getWindowLayoutJson());

    // `|| hiddenByMasterMinimise` records what the user actually chose,
    // not the transient hidden state a minimised master imposes - see
    // setHiddenByMasterMinimise(). Without it, quitting while minimised
    // saves every satellite as hidden.
    states[windowId] = { getBounds(), isVisible() || hiddenByMasterMinimise };

    settings.setWindowLayoutJson(WindowLayoutStore::toJson(states));
    settings.save();
}

