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
}
