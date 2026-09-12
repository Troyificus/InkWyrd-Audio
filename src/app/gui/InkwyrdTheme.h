#pragma once

#include <juce_graphics/juce_graphics.h>

// The palette, in one place, because it is the thing most likely to be
// adjusted by eye after seeing it on a real screen - and, since skins,
// the thing a user can replace at runtime.
//
// The built-in values were taken from the design mockup by eye rather
// than sampled: the mockup arrived as an image in conversation, not a
// file, so they are close readings rather than exact values.
//
// THESE NAMES ARE READ AT 40-ODD PAINT SITES and are deliberately kept
// as plain names rather than becoming `palette().accent` everywhere -
// that churn would have touched every drawing function for no gain.
// The cost is that they are mutable globals:
//
//   MESSAGE THREAD ONLY. Painting is their only reader and skins are
//   applied from the message thread, so no locking is needed. Nothing on
//   the audio thread may ever read them.
namespace inkwyrd::theme
{
    // Everything one skin can set. A skin file leaves out whatever it
    // doesn't care about, and those fields keep the built-in values - so
    // a skin written today still works when a later version adds a
    // colour to this list.
    struct Palette
    {
        // The desktop behind everything, and the deepest wells inside a
        // window (list backgrounds, the album-art slot).
        juce::Colour background     { 0xff070c09 };
        juce::Colour panelDeep      { 0xff0a120d };

        // A window's body, and the slightly raised blocks on it.
        juce::Colour panel          { 0xff0f1c15 };
        juce::Colour panelRaised    { 0xff14251c };

        // Title bars are the one INVERTED surface in the design: a bright
        // green bar carrying near-black text. Everything else is light on
        // dark.
        juce::Colour titleBar       { 0xff7fcf9f };
        juce::Colour titleBarText   { 0xff08130d };
        juce::Colour titleBarSubtle { 0xff2a4d38 };

        // Body text. `text` is the readable default; `textDim` is for
        // captions and column headings, which sit noticeably back in the
        // mockup rather than being merely smaller.
        juce::Colour text           { 0xff8fe3ad };
        juce::Colour textDim        { 0xff59a37b };

        // The live/selected green - now-playing rows, the active plugin,
        // meter fill, focus rings.
        juce::Colour accent         { 0xff4fe08a };
        juce::Colour accentSoft     { 0xff2f7f52 };

        // Borders. `outline` is the visible 1px edge on panels and
        // buttons; `outlineFaint` separates rows inside a list, where a
        // full-strength line would look like a grid.
        juce::Colour outline        { 0xff2a5c42 };
        juce::Colour outlineFaint   { 0xff17301f };

        juce::Colour warning        { 0xffe0b24f };
        juce::Colour danger         { 0xffe06a5a };

        // Corner radius used throughout - panels, buttons, pads, fields.
        float cornerRadius = 6.0f;

        // Title bar height. Taller than a normal window's because it
        // carries a logo and two lines of text. Clamped on load: a window
        // whose title bar is two pixels tall can't be dragged.
        int titleBarHeight = 46;

        // Font FAMILIES, not files. Nothing is bundled - a font is a
        // licensing decision, not a styling one - so a name nobody has
        // installed falls back to JUCE's default rather than failing.
        juce::String titleFontName { "Segoe UI Semibold" };
        juce::String labelFontName { "Segoe UI" };
        juce::String digitFontName { "Consolas" };

        static constexpr int kMinTitleBarHeight = 28;
        static constexpr int kMaxTitleBarHeight = 80;
    };

    // The live values. Assigned by applyPalette(), read by every paint.
    inline juce::Colour background      { Palette{}.background };
    inline juce::Colour panelDeep       { Palette{}.panelDeep };
    inline juce::Colour panel           { Palette{}.panel };
    inline juce::Colour panelRaised     { Palette{}.panelRaised };
    inline juce::Colour titleBar        { Palette{}.titleBar };
    inline juce::Colour titleBarText    { Palette{}.titleBarText };
    inline juce::Colour titleBarSubtle  { Palette{}.titleBarSubtle };
    inline juce::Colour text            { Palette{}.text };
    inline juce::Colour textDim         { Palette{}.textDim };
    inline juce::Colour accent          { Palette{}.accent };
    inline juce::Colour accentSoft      { Palette{}.accentSoft };
    inline juce::Colour outline         { Palette{}.outline };
    inline juce::Colour outlineFaint    { Palette{}.outlineFaint };
    inline juce::Colour warning         { Palette{}.warning };
    inline juce::Colour danger          { Palette{}.danger };

    inline float cornerRadius = Palette{}.cornerRadius;
    inline int titleBarHeight = Palette{}.titleBarHeight;

    // Kept whole as well as unpacked, so the Settings screen can export
    // exactly what is on screen and the font names have somewhere to
    // live.
    inline Palette activePalette {};

    // A default-constructed Palette IS the built-in look - the built-in
    // values are the field initialisers above, so there is only one copy
    // of them.
    inline Palette builtIn() { return {}; }

    inline const Palette& current() { return activePalette; }

    inline void applyPalette(const Palette& palette)
    {
        activePalette = palette;

        background     = palette.background;
        panelDeep      = palette.panelDeep;
        panel          = palette.panel;
        panelRaised    = palette.panelRaised;
        titleBar       = palette.titleBar;
        titleBarText   = palette.titleBarText;
        titleBarSubtle = palette.titleBarSubtle;
        text           = palette.text;
        textDim        = palette.textDim;
        accent         = palette.accent;
        accentSoft     = palette.accentSoft;
        outline        = palette.outline;
        outlineFaint   = palette.outlineFaint;
        warning        = palette.warning;
        danger         = palette.danger;

        cornerRadius = palette.cornerRadius;
        titleBarHeight = juce::jlimit(Palette::kMinTitleBarHeight,
                                       Palette::kMaxTitleBarHeight,
                                       palette.titleBarHeight);
    }

    inline void resetToBuiltIn() { applyPalette(builtIn()); }
}
