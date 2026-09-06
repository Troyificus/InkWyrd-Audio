#pragma once

#include <map>

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

// A DetachableWindow's persisted bounds and visibility. Position/size
// only - not maximised/minimised state, which none of these windows
// support (they're small satellite/utility windows, not the whole app).
struct WindowState
{
    juce::Rectangle<int> bounds;
    bool visible = true;
};

// Converts the whole multi-window layout to/from the single JSON blob
// AppSettings stores it as (PropertiesFile only holds flat scalars, and
// this is a variable-length, variable-window-count structure), and
// clamps a restored position back onto a currently-connected display -
// a window remembered from a monitor that's since been unplugged must
// not come back up permanently unreachable off-screen.
namespace WindowLayoutStore
{
    // Malformed or empty input yields an empty map rather than throwing
    // or crashing - this string lives in a plaintext, technically
    // user-editable settings file, and a first launch has no blob at all.
    std::map<juce::String, WindowState> fromJson(const juce::String& json);

    juce::String toJson(const std::map<juce::String, WindowState>& states);

    // If `bounds` already overlaps some currently-connected display,
    // it's left exactly as is - a window partly off-screen is the
    // user's own choice, dragging it back is on them. Otherwise it's
    // repositioned (never resized beyond what the target display can
    // hold) onto the display nearest its old centre, or the primary
    // display if that lookup fails.
    juce::Rectangle<int> clampToNearestDisplay(juce::Rectangle<int> bounds);

    // The primary display's usable area, or a 1920x1080 guess if no
    // display info is available at all. Shared by every window's
    // first-launch default-position calculation so each doesn't need its
    // own display-lookup fallback.
    juce::Rectangle<int> primaryDisplayArea();
}
