#pragma once

// Every sprite name the app asks a skin for, and every component ID a
// control carries so a skin can give it art of its own.
//
// One list, because three things have to agree on it: the painting code
// that asks, SkinSprites' typo check on a loaded skin, and the
// skin-builder tool (tools/skin-builder) that generates a sheet. The
// tool reads THIS FILE, so adding a name here is enough to make it show
// up in a generated template.
//
// Per-control variants are "<base>.<componentId>" - e.g. "button" is
// every button, "button.transport.play" is only the play button, and
// "icon.transport.play" is a picture drawn on it in place of its label.
namespace inkwyrd::sprites
{
    // Generic bases. States are added as "@over", "@down", "@on",
    // "@onover", "@ondown", "@disabled", "@focus".
    inline constexpr const char* kBaseNames[] =
    {
        "window",            // a window's whole body, behind everything
        "titlebar",          // the title bar strip; "titlebar@inactive" optional
        "titlebutton",       // background of minimise / maximise / close
        "button",            // every text button
        "checkbox",          // the box of a tick box, natural size
        "slider.track",      // horizontal slider groove
        "slider.fill",       // the part of the groove up to the value
        "slider.thumb",      // the knob, natural size
        "scrollbar.track",
        "scrollbar.thumb",
        "textbox",           // text fields; "@focus" while typing
        "combobox",
        "popup",             // popup menu background
        "panel",             // framed blocks inside a window
        "panel.raised",
        "well",              // recessed areas
        "display",           // the Player's now-playing screen
    };

    // Bases that only exist per control: an icon is always a specific
    // control's picture, there is no "icon for every button".
    inline constexpr const char* kPerControlOnlyBases[] = { "icon" };

    // Component IDs. Set with setComponentID() on the control; the
    // LookAndFeel reads them back when painting.
    inline constexpr const char* kComponentIds[] =
    {
        // Player transport
        "transport.play",
        "transport.pause",
        "transport.stop",
        "transport.fadeout",
        "transport.skip",

        // Player toggles - lit ("@on") while the option is engaged
        "toggle.shuffle",
        "toggle.mute",
        "toggle.monitor",
        "toggle.crossfade",
        "toggle.loop",

        // Player window activators and Settings
        "open.playlist",
        "open.library",
        "open.voicefx",
        "open.soundboard",
        "open.scenes",
        "open.settings",

        // Player sliders
        "crossfade",
        "loopgap",
        "fadeout",
        "master",
        "mic",

        // Title bar glyphs
        "close",
        "minimise",
        "maximise",
    };
}
