#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

// Pure geometry, deliberately free of any Component/peer dependency, so
// it can be exercised headlessly from audio-engine-test the same way the
// engine's own logic is - a magnetic-snap bug is exactly the kind of
// thing worth catching without launching the app.
//
// This is the ONE function both single-window edge-snap (against the
// screen) and multi-window magnetism (against sibling windows) build on:
// the screen case is just "obstacles" containing zero rectangles.
namespace inkwyrd
{
    // Returns `candidate` translated (never resized) so that, per axis
    // independently, its nearest edge lines up with the nearest edge of
    // `screenArea` or any rectangle in `obstacles` that is within
    // `threshold` pixels - covering all four meaningful alignments per
    // axis (e.g. horizontally: left-to-left, left-to-right, right-to-left,
    // right-to-right, so a window can dock flush against either side of
    // another, or line up its edge with it). If nothing is within
    // threshold on an axis, that axis is left untouched.
    juce::Rectangle<int> snapRectangle(juce::Rectangle<int> candidate,
                                        const juce::Array<juce::Rectangle<int>>& obstacles,
                                        juce::Rectangle<int> screenArea,
                                        int threshold);
}
