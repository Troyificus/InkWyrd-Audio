#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AppSettings.h"

// Shared base for every top-level window in the Winamp-style layout
// (Player, Playlist, Library, Voice FX, Soundboard). Phase 0 of that
// work: bounds persist to AppSettings' single window-layout blob (via
// WindowLayoutStore) and restore, clamped onto a currently-connected
// display, at construction. Magnetic snapping and the custom title bar
// that live-drag interception needs come in a later phase - this base
// still uses a native title bar for now.
//
// Visibility is tracked too (WindowState carries it), but what a
// subclass DOES with wasVisibleWhenSaved() is up to it: MainWindow (the
// only window today) always shows itself regardless, since there is no
// way to un-hide it yet. Satellite windows added later use it to come
// back up hidden if that's how the user left them.
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
    DetachableWindow(const juce::String& windowName, juce::String windowId,
                      AppSettings& settingsToUse, juce::Rectangle<int> defaultBounds,
                      bool defaultVisible = true);

    ~DetachableWindow() override;

    // Every currently-constructed DetachableWindow, in construction
    // order. Needed for magnetic snapping later and for "hide every
    // satellite before a Setup-view transition destroys what they point
    // into" now.
    static const juce::Array<DetachableWindow*>& getActiveWindows() { return activeWindows; }

    const juce::String& getWindowId() const { return windowId; }
    bool wasVisibleWhenSaved() const { return restoredVisible; }

    void moved() override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void persistNow();

    juce::String windowId;
    AppSettings& settings;
    bool restoredVisible;

    static juce::Array<DetachableWindow*> activeWindows;
};
