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

    // True when `a` and `b` are docked: flush (within `tolerance`) along one
    // axis while genuinely OVERLAPPING on the other. The overlap requirement
    // is the important half - without it two windows that merely clip past
    // each other's corner, sharing no actual edge, would count as attached
    // and get dragged around together.
    bool areRectanglesDocked(juce::Rectangle<int> a, juce::Rectangle<int> b, int tolerance);

    // Like snapRectangle, but for a RESIZE: only the edges actually
    // being dragged move, so the window is reshaped rather than slid.
    // An edge snaps to any obstacle or screen edge within `threshold`,
    // which is what lets a window stretched towards its neighbour land
    // exactly flush with it instead of a few pixels short.
    juce::Rectangle<int> snapResizedEdges(juce::Rectangle<int> candidate,
                                           const juce::Array<juce::Rectangle<int>>& obstacles,
                                           juce::Rectangle<int> screenArea,
                                           int threshold,
                                           bool stretchingLeft,
                                           bool stretchingRight,
                                           bool stretchingTop,
                                           bool stretchingBottom);

    // Indices of every rectangle transitively docked to rectangles[startIndex],
    // excluding startIndex itself. Transitive on purpose: dragging one window
    // should carry a whole flush-attached chain (A-B-C moves as one when you
    // grab A), not just its immediate neighbours.
    juce::Array<int> findDockedGroup(const juce::Array<juce::Rectangle<int>>& rectangles,
                                      int startIndex,
                                      int tolerance);
}
