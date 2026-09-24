#pragma once

#include <juce_core/juce_core.h>

#include "InkwyrdTheme.h"
#include "SkinSprites.h"

namespace inkwyrd
{
    // Reading and writing skins: %APPDATA%\Inkwyrd Audio\skins\<Name>\skin.json
    //
    // A FOLDER per skin rather than a bare .json, because a skin can also
    // carry a logo image, and a folder is the thing a user can zip and
    // send to someone else as one piece.
    //
    // Pure over juce::var and juce::File - no window, no LookAndFeel - so
    // the parsing rules are covered by the headless self-test rather than
    // by opening Settings and looking. Nothing here throws or asserts on
    // bad input: a skin comes from a user's text editor, so malformed is
    // an expected case, not an exceptional one.
    struct SkinLoadResult
    {
        bool ok = false;

        // Anything the file didn't set keeps its built-in value, so a
        // skin written today still works when a later version adds a
        // colour.
        theme::Palette palette;

        juce::String name;

        // The skin's own revision ("version" in skin.json; 0 if it has none).
        // Bump it whenever a skin you share changes. For the skins the app
        // ships it's what decides whether a user's copy gets updated - see
        // ExampleSkins.h.
        int version = 0;

        // Empty when the skin has no logo, or names one that isn't there
        // (which is a warning, not a failure - the drawn mark is used).
        juce::File logoFile;

        // Font files the skin carries in its own folder ("fontFiles"), so it
        // can use a typeface the user doesn't have installed. Its "fonts"
        // section then names those families like any other.
        juce::Array<juce::File> fontFiles;

        // Per-widget images, from the optional "sprites" section. Empty
        // when the skin has none, or when they couldn't be used - either
        // way every control falls back to the drawn look.
        SkinSprites sprites;

        // Set when ok == false: why it couldn't be used.
        juce::String message;

        // Things worth telling the author that didn't stop the skin
        // loading: unknown keys, unreadable colours, a clamped size.
        juce::StringArray warnings;
    };

    class SkinLoader
    {
    public:
        // Same skip-if-newer discipline as every other stored file here: a
        // skin written by a later version is refused rather than
        // half-read.
        static constexpr int kCurrentSchemaVersion = 1;

        static constexpr const char* kSkinFileName = "skin.json";

        static juce::File getDefaultFolder();

        // The skins in a folder: subfolders holding a skin.json, sorted by
        // name. Doesn't parse them - a broken skin still gets listed, so
        // its author can see it and read the error.
        static juce::Array<juce::File> findSkinFolders(const juce::File& skinsFolder);

        // Reads <skinFolder>/skin.json.
        static SkinLoadResult loadFromFolder(const juce::File& skinFolder);

        // The parsing itself. `skinFolder` is only used to resolve the
        // logo and to name an unnamed skin, so this can be tested with no
        // files at all.
        static SkinLoadResult parse(const juce::var& json, const juce::File& skinFolder);

        // What the export button writes: the palette as it stands, in the
        // same shape parse() reads, so export -> edit -> load round-trips.
        static juce::var toVar(const theme::Palette& palette, const juce::String& name,
                                const juce::String& logoFileName = {}, int version = 1);

        static bool writeToFolder(const theme::Palette& palette, const juce::String& name,
                                   const juce::File& skinFolder, juce::String& errorMessage);

        // "#rrggbb", "#aarrggbb", and the same without the hash. Anything
        // else is refused rather than guessed at.
        static bool parseColour(const juce::String& text, juce::Colour& result);
        static juce::String colourToString(juce::Colour colour);
    };
}
