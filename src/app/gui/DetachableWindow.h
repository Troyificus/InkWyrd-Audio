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
// Phase 2: magnetism. Windows end up flush against each other and the
// screen edges, and dragging one carries anything docked to it.
//
// HOW, and why not the obvious ways - both were built and measured
// before landing on this:
//
//  * NOT a ComponentBoundsConstrainer. JUCE does route title-bar drags
//    through the constrainer, so this looks right and even compiles, but
//    on Windows it cannot work: for a MOVE (all four stretch flags
//    false) HWNDComponentPeer::getConstrainedBounds takes the
//    constrainer's modified SIZE and then explicitly forces the position
//    back to the requested one (`.withPosition (requestedPhysicalClient
//    .getPosition())`), discarding any repositioning. Verified by real
//    drags: the window tracked the mouse and never snapped, at 3px and
//    7px from a flush edge.
//  * NOT mouseDown/mouseDrag/mouseUp either. The peer reports the title
//    bar as HTCAPTION (juce_Windowing_windows.cpp, WM_NCHITTEST ->
//    Kind::caption), so Windows itself performs the drag and JUCE never
//    sees the mouse events for it - anything keyed off them silently
//    never runs.
//
// What's left, and what this uses: moved() fires throughout a native
// drag regardless of who is driving it. Docked windows are translated
// live from there, and the snap itself is applied once movement settles
// (kSettleMs after the last moved()). So the group follows in real time
// and the window lands flush a moment after release - the one part that
// isn't strictly live, because Windows owns the position mid-drag and
// fighting it produces judder rather than magnetism.
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
    // what made one vanish with no way back (it left JUCE thinking the
    // window was still visible, so the activator button's toggle hid it
    // outright instead of restoring it). Only the master Player window
    // gets a minimise button, and minimising it takes the satellites
    // down with it - see PlayerWindow.
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
    // launch, having silently recorded a transient state as intent.
    void setHiddenByMasterMinimise(bool shouldBeHidden);

    // Satellites hide rather than close. PlayerWindow and MainWindow
    // override this again to quit the app instead.
    void closeButtonPressed() override;

    void moved() override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void persistNow();

    // Snap this window's edges to the screen and to other windows, and
    // carry the docked group along by the same amount so a docked pair
    // doesn't shear apart when the leader lands.
    void applySnap();

    // Snapshot what's docked to this window right now. Taken once at the
    // start of a movement burst so a window this drag snaps against
    // partway through doesn't retroactively join and start following.
    void captureDockedGroup(juce::Rectangle<int> boundsToTestFrom);

    // Every other window that counts as a snap target or drag companion:
    // visible, not minimised, not this one.
    juce::Array<DetachableWindow*> otherLiveWindows() const;
    bool isCarrying(const DetachableWindow* window) const;
    void translateDockedGroup(juce::Point<int> delta);

    juce::String windowId;
    AppSettings& settings;
    bool restoredVisible;
    bool hiddenByMasterMinimise = false;

    // SafePointer rather than raw: a companion could be destroyed
    // mid-drag (Settings tearing the layout down), and a stale raw
    // pointer here would be a use-after-free on the next move.
    juce::Array<juce::Component::SafePointer<DetachableWindow>> dockedGroup;
    juce::Point<int> lastMovedPosition;
    bool movementInProgress = false;
    bool applyingSnap = false;

    static juce::Array<DetachableWindow*> activeWindows;

    // True only while some window is propagating a move to its docked
    // companions. Any window that moves during that window of time is
    // being CARRIED, not dragged, and must not run leader logic - see
    // translateDockedGroup() for what happens without this.
    static bool groupMoveInProgress;
};
