#include "WindowSnapping.h"

#include <cstdlib>

namespace
{
    // Smallest-magnitude translation that lines candidate's `edgeA` OR
    // `edgeB` (its two parallel edges on this axis, e.g. left/right) up
    // with any value in `targets`, within `threshold`. 0 if nothing
    // qualifies - the axis is then left exactly where it was.
    int bestSnapDelta(int edgeA, int edgeB, const juce::Array<int>& targets, int threshold)
    {
        int best = 0;
        int bestAbs = threshold + 1;

        for (auto target : targets)
        {
            for (auto edge : { edgeA, edgeB })
            {
                auto delta = target - edge;
                auto deltaAbs = std::abs(delta);
                if (deltaAbs < bestAbs)
                {
                    bestAbs = deltaAbs;
                    best = delta;
                }
            }
        }

        return bestAbs <= threshold ? best : 0;
    }
}

namespace inkwyrd
{
    juce::Rectangle<int> snapRectangle(juce::Rectangle<int> candidate,
                                        const juce::Array<juce::Rectangle<int>>& obstacles,
                                        juce::Rectangle<int> screenArea,
                                        int threshold)
    {
        juce::Array<int> xTargets; // vertical lines: screen/obstacle left+right edges
        juce::Array<int> yTargets; // horizontal lines: screen/obstacle top+bottom edges

        xTargets.add(screenArea.getX());
        xTargets.add(screenArea.getRight());
        yTargets.add(screenArea.getY());
        yTargets.add(screenArea.getBottom());

        for (auto& obstacle : obstacles)
        {
            xTargets.add(obstacle.getX());
            xTargets.add(obstacle.getRight());
            yTargets.add(obstacle.getY());
            yTargets.add(obstacle.getBottom());
        }

        auto deltaX = bestSnapDelta(candidate.getX(), candidate.getRight(), xTargets, threshold);
        auto deltaY = bestSnapDelta(candidate.getY(), candidate.getBottom(), yTargets, threshold);

        return candidate.withPosition(candidate.getX() + deltaX, candidate.getY() + deltaY);
    }

    bool areRectanglesDocked(juce::Rectangle<int> a, juce::Rectangle<int> b, int tolerance)
    {
        // Strictly positive overlap, not >= 0: two windows that only just
        // touch corner-to-corner share a single point, no edge, and moving
        // one shouldn't drag the other.
        auto verticalOverlap = juce::jmin(a.getBottom(), b.getBottom()) - juce::jmax(a.getY(), b.getY());
        auto horizontalOverlap = juce::jmin(a.getRight(), b.getRight()) - juce::jmax(a.getX(), b.getX());

        // Side by side: one's right edge meeting the other's left.
        if (verticalOverlap > 0
            && (std::abs(a.getRight() - b.getX()) <= tolerance
                || std::abs(b.getRight() - a.getX()) <= tolerance))
            return true;

        // Stacked: one's bottom edge meeting the other's top.
        if (horizontalOverlap > 0
            && (std::abs(a.getBottom() - b.getY()) <= tolerance
                || std::abs(b.getBottom() - a.getY()) <= tolerance))
            return true;

        return false;
    }

    juce::Array<int> findDockedGroup(const juce::Array<juce::Rectangle<int>>& rectangles,
                                      int startIndex,
                                      int tolerance)
    {
        juce::Array<int> group;
        if (! juce::isPositiveAndBelow(startIndex, rectangles.size()))
            return group;

        // Breadth/depth doesn't matter here, only reachability - walk out
        // from the dragged rectangle collecting anything attached to
        // something already in the group.
        juce::Array<int> frontier;
        frontier.add(startIndex);

        while (! frontier.isEmpty())
        {
            auto current = frontier.removeAndReturn(frontier.size() - 1);

            for (int i = 0; i < rectangles.size(); ++i)
            {
                if (i == startIndex || group.contains(i))
                    continue;

                if (areRectanglesDocked(rectangles[current], rectangles[i], tolerance))
                {
                    group.add(i);
                    frontier.add(i);
                }
            }
        }

        return group;
    }
}
