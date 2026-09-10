#pragma once

#include <juce_graphics/juce_graphics.h>

// The palette, in one place, because it is the thing most likely to be
// adjusted by eye after seeing it on a real screen.
//
// Taken from the design mockup by eye rather than sampled - the mockup
// arrived as an image in conversation, not a file, so these are close
// readings rather than exact values. If something looks off against the
// original, this table is the only place to change it.
namespace inkwyrd::theme
{
    // The desktop behind everything, and the deepest wells inside a
    // window (list backgrounds, the album-art slot).
    const juce::Colour background      { 0xff070c09 };
    const juce::Colour panelDeep       { 0xff0a120d };

    // A window's body, and the slightly raised blocks on it.
    const juce::Colour panel           { 0xff0f1c15 };
    const juce::Colour panelRaised     { 0xff14251c };

    // Title bars are the one INVERTED surface in the design: a bright
    // green bar carrying near-black text. Everything else is light on
    // dark.
    const juce::Colour titleBar        { 0xff7fcf9f };
    const juce::Colour titleBarText    { 0xff08130d };
    const juce::Colour titleBarSubtle  { 0xff2a4d38 };

    // Body text. `text` is the readable default; `textDim` is for
    // captions and column headings, which sit noticeably back in the
    // mockup rather than being merely smaller.
    const juce::Colour text            { 0xff8fe3ad };
    const juce::Colour textDim         { 0xff59a37b };

    // The live/selected green - now-playing rows, the active plugin,
    // meter fill, focus rings.
    const juce::Colour accent          { 0xff4fe08a };
    const juce::Colour accentSoft      { 0xff2f7f52 };

    // Borders. `outline` is the visible 1px edge on panels and buttons;
    // `outlineFaint` separates rows inside a list, where a full-strength
    // line would look like a grid.
    const juce::Colour outline         { 0xff2a5c42 };
    const juce::Colour outlineFaint    { 0xff17301f };

    const juce::Colour warning         { 0xffe0b24f };
    const juce::Colour danger          { 0xffe06a5a };

    // Corner radius used throughout - panels, buttons, pads, fields.
    constexpr float cornerRadius = 6.0f;

    // Title bar height. Taller than a normal window's because it
    // carries a logo and two lines of text.
    constexpr int titleBarHeight = 46;
}
