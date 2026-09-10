#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AppSettings.h"

// Shared base for every top-level window in the Winamp-style layout
// (Player, Playlist, Library, Voice FX, Soundboard).
//
// Phase 0: bounds persist to AppSettings' single window-layout blob (via
// WindowLayoutStore) and restore, clamped onto a currently-connected
// display, at construction.
//
// Phase 2: magnetism - windows pull flush against each other and the
// screen edges as they're dragged, and dragging the master window
// carries anything docked to it.
//
// HOW, after two mechanisms that look right and aren't:
//
//  * NOT a ComponentBoundsConstrainer. JUCE does route title-bar drags
//    through the constrainer, and this compiles and runs, but on Windows
//    HWNDComponentPeer::getConstrainedBounds takes only the constrainer's
//    modified SIZE for a move and forces the position back to the
//    requested one - repositioning is discarded. Measured: windows
//    tracked the mouse and never snapped, 3px and 7px from flush.
//  * NOT mouseDown/mouseDrag/mouseUp. The peer reports the title bar as
//    HTCAPTION, so Windows performs the drag itself and JUCE never sees
//    those events.
//
// What works, and what this uses: hooking the window's own WM_MOVING and
// WM_SIZING. That is the standard Win32 way to do magnetic windows - the
// OS asks "where should this window go?" before moving it, and the
// answer can be adjusted. Doing it there means the window visibly snaps
// DURING the drag rather than settling into place afterwards, and it
// gives resize-snapping for free, which nothing above could. The
// adjusted rectangle is passed on to JUCE's own handler rather than
// swallowed, so JUCE's size limits still apply on top.
//
// Everything in that hook works in PHYSICAL screen pixels - the units
// Windows uses for these messages - so no logical/physical conversion is
// needed and it behaves the same at any display scaling.
class DetachableWindow : public juce::DocumentWindow,
                          private juce::Timer
{
public:
    // windowId is the stable key this window's state is stored under -
    // must be unique and unchanging across releases (it's a string in a
    // persisted settings file, not an enum), e.g. "player", "voiceFx".
    // defaultVisible is only used the FIRST time this windowId is ever
    // seen (no saved entry yet) - Player/Playlist/Library want to open
    // on a clean first launch, while Voice FX/Soundboard start hidden so
    // a first run isn't five overlapping windows.
    // titleBarButtons: satellites get closeButton only - X already means
    // "hide" for them, and a per-satellite minimise button is exactly
    // what made one vanish with no way back. Only the master Player
    // window gets a minimise button, and minimising it takes the
    // satellites down with it - see PlayerWindow.
    DetachableWindow(const juce::String& windowName, juce::String windowId,
                      AppSettings& settingsToUse, juce::Rectangle<int> defaultBounds,
                      bool defaultVisible = true,
                      int titleBarButtons = juce::DocumentWindow::closeButton);

    ~DetachableWindow() override;

    // Every currently-constructed DetachableWindow, in construction
    // order. Needed for snapping, for carrying docked windows along on a
    // drag, and for "hide every satellite before a Setup-view transition
    // destroys what they point into".
    static const juce::Array<DetachableWindow*>& getActiveWindows() { return activeWindows; }

    const juce::String& getWindowId() const { return windowId; }
    bool wasVisibleWhenSaved() const { return restoredVisible; }

    // Hide/show this window because the MASTER window minimised, rather
    // than because the user chose to hide it. The saved layout keeps
    // recording it as visible throughout - otherwise quitting while
    // minimised would bring every satellite back hidden on the next
    // launch, having recorded a transient state as intent.
    void setHiddenByMasterMinimise(bool shouldBeHidden);

    // Satellites hide rather than close. PlayerWindow and MainWindow
    // override this again to quit the app instead.
    void closeButtonPressed() override;

    // Whether dragging THIS window carries anything docked to it. Only
    // the master (Player) window does. Satellites deliberately do not:
    // if every window carried its neighbours, a docked satellite could
    // never be pulled off the group - dragging it just took the whole
    // cluster along. Drag a satellite to detach it, drag the main window
    // to move everything at once.
    virtual bool carriesDockedWindows() const { return false; }

    // Called ONLY from the native window-procedure hook. Public because
    // that hook is a free function rather than a member - not part of
    // this class's real interface. `nativeRect` is a Win32 RECT* in
    // physical screen pixels, void* so this header doesn't drag in
    // windows.h.
    void beginNativeDragFromHook();
    void endNativeDragFromHook();
    void applyMoveSnapFromHook(void* nativeRect);
    void applyResizeSnapFromHook(void* nativeRect, int edge);

    void moved() override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void persistNow();

    // Snapshot what's docked to this window, taken once as a drag
    // begins so a window this drag snaps against partway through doesn't
    // retroactively join and start following.
    void captureDockedGroup(juce::Rectangle<int> boundsToTestFrom);
    void translateDockedGroup(juce::Point<int> delta);

    // Every other window that counts as a snap target or drag companion:
    // visible, not minimised, not this one.
    juce::Array<DetachableWindow*> otherLiveWindows() const;
    bool isCarrying(const DetachableWindow* window) const;

    // Native drag/resize interception - see the class comment.
    void installNativeHookIfNeeded();
    void removeNativeHook();

    // Rectangles of the other live windows, in PHYSICAL screen pixels,
    // excluding any this drag is already carrying (a carried window is
    // flush by definition, so letting it act as a magnet would pin the
    // group where it started).
    juce::Array<juce::Rectangle<int>> physicalObstacles() const;

    juce::String windowId;
    AppSettings& settings;
    bool restoredVisible;
    bool hiddenByMasterMinimise = false;

    // SafePointer rather than raw: a companion could be destroyed
    // mid-drag (Settings tearing the layout down), and a stale raw
    // pointer here would be a use-after-free on the next move.
    juce::Array<juce::Component::SafePointer<DetachableWindow>> dockedGroup;
    juce::Point<int> lastMovedPosition;
    bool nativeDragActive = false;

    void* hookedWindowHandle = nullptr;

    // Where the window and cursor were when the current drag/resize
    // began. The snap is applied to a position rebuilt from these, not
    // to the one Windows proposes - otherwise the snap feeds back into
    // its own input and the window can never be pulled off whatever it
    // first stuck to. See applyMoveSnapFromHook().
    juce::Rectangle<int> dragStartBounds;
    int dragStartCursorX = 0;
    int dragStartCursorY = 0;
    bool dragStartValid = false;

    static juce::Array<DetachableWindow*> activeWindows;

    // True only while some window is propagating a move to its docked
    // companions. Any window that moves during that is being CARRIED,
    // not dragged, and must not run leader logic - without this the two
    // windows push each other off the screen and take the stack with
    // them (a real 0xc000041d crash, reported from beta.11).
    static bool groupMoveInProgress;
};
