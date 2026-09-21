#pragma once

#include <juce_core/juce_core.h>

namespace inkwyrd
{
    // A small fixed palette rather than a full ColourSelector: on a board
    // meant to be scanned at a glance mid-session, a handful of clearly
    // distinct colours is more useful than a colour wheel. Shared by the
    // soundboard and the scenes, so a colour means the same thing in both.
    struct PresetColour
    {
        const char* name;
        juce::uint32 argb;
    };

    inline constexpr PresetColour kPresetColours[] = {
        { "Slate",  0xff2a3a33 },
        { "Red",    0xff8c2f2f },
        { "Orange", 0xff9c5a1e },
        { "Yellow", 0xff8a7a1e },
        { "Green",  0xff2f7f52 },
        { "Teal",   0xff1e6b6b },
        { "Blue",   0xff2f4a8c },
        { "Purple", 0xff5a2f8c },
    };

    inline constexpr int kNumPresetColours = (int) (sizeof(kPresetColours) / sizeof(kPresetColours[0]));
}
