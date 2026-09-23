#pragma once

#include <map>
#include <memory>

#include <juce_graphics/juce_graphics.h>

namespace inkwyrd
{
    // Per-widget images for a skin: the optional "sprites" section of
    // skin.json, pointing at a sprite sheet in the skin's folder.
    //
    //   "sprites": {
    //     "sheet":   "sprites.png",
    //     "sheet2x": "sprites@2x.png",   // optional, exactly twice the size
    //     "scale":   2,                   // screen pixels per sheet pixel
    //     "pixelArt": true,               // nearest-neighbour, no smoothing
    //     "items": {
    //       "button":        { "rect": [0, 0, 24, 16], "slice": [6, 6, 6, 6] },
    //       "button@down":   { "rect": [24, 0, 24, 16], "slice": [6, 6, 6, 6] },
    //       "icon.transport.play": { "rect": [0, 32, 9, 9] },
    //       "window":        { "rect": [...], "slice": [...], "tile": true }
    //     }
    //   }
    //
    // EVERYTHING IS OPTIONAL, per item. A widget with no sprite is drawn
    // exactly as it was before sprites existed, so a skin can replace the
    // buttons and nothing else. That is also what keeps a skin written
    // today working when a later version adds new sprite names.
    //
    // Every stretched sprite is NINE-SLICE: `slice` is [left, top, right,
    // bottom] in sheet pixels, the corners stay their real size, and the
    // edges and middle stretch (or repeat, with "tile": true). That is
    // what lets a pixel-art skin survive the windows being resizable,
    // which fixed-size bitmaps in the Winamp manner could not.
    //
    // Pure over juce::var and juce::Image - no LookAndFeel, no window - so
    // the rules a skin author hits are covered by the headless self-test.
    struct SpriteItem
    {
        // In 1x sheet pixels. The 2x sheet uses the same numbers doubled.
        juce::Rectangle<int> rect;
        juce::BorderSize<int> slice;
        bool tile = false;

        // Cut out of the sheets once at load, so painting never slices.
        juce::Image image1x, image2x;
    };

    // The states a control can be drawn in. Each falls back through a
    // chain of less specific sprites - see stateSuffixes() - so an author
    // can draw only "button" and "button@down" and have the rest follow.
    enum class SpriteState
    {
        normal, over, down, on, onOver, onDown, disabled, focus
    };

    class SkinSprites
    {
    public:
        static constexpr const char* kSpritesKey = "sprites";

        // Guards against a shared skin that would eat memory behind every
        // paint: one 4096 x 4096 sheet is 64 MB decoded.
        static constexpr int kMaxSheetPixels = 4096;
        static constexpr juce::int64 kMaxSheetBytes = 16 * 1024 * 1024;

        static constexpr float kMinScale = 1.0f;
        static constexpr float kMaxScale = 8.0f;

        // Reads the "sprites" object, loading its sheets from skinFolder.
        // Never fails the skin: a problem with the sprites is a warning,
        // and the result simply has no sprites (the drawn look is used).
        static SkinSprites parse(const juce::var& spritesJson, const juce::File& skinFolder,
                                  juce::StringArray& warnings);

        // The same, with the sheets already in hand - what the self-test
        // uses, so it needs no files.
        static SkinSprites parseWithImages(const juce::var& spritesJson,
                                            const juce::Image& sheet, const juce::Image& sheet2x,
                                            juce::StringArray& warnings);

        bool isEmpty() const { return items.empty(); }
        int size() const { return (int) items.size(); }
        float getScale() const { return scale; }
        bool isPixelArt() const { return pixelArt; }
        bool hasHiResSheet() const { return hasSheet2x; }

        const SpriteItem* find(const juce::String& name) const;

        // The first sprite that exists for `base` in `state`, walking the
        // fallback chain. With a non-empty componentId, "<base>.<id>" is
        // tried through the WHOLE chain first: a play button with its own
        // art uses that art even when only the generic button has a
        // pressed state.
        //
        // `exactState` says whether what was found is really that state,
        // or a fallback - callers dim a disabled control that had no
        // disabled art of its own.
        const SpriteItem* findForState(const juce::String& base, SpriteState state,
                                        const juce::String& componentId = {},
                                        bool* exactState = nullptr) const;

        // Stretched nine-slice into `dest`. Corners shrink in proportion
        // rather than overlap when dest is smaller than they are.
        void drawNineSlice(juce::Graphics& g, const SpriteItem& item, juce::Rectangle<float> dest,
                            float alpha = 1.0f) const;

        // At its own size (times the skin's scale), centred in `area`.
        // For icons, checkboxes and slider thumbs.
        void drawNatural(juce::Graphics& g, const SpriteItem& item, juce::Rectangle<float> area,
                          float alpha = 1.0f) const;

        juce::Rectangle<float> naturalBounds(const SpriteItem& item) const;

        // "@down", "@over", "" and so on, most specific first.
        static juce::StringArray stateSuffixes(SpriteState state);

        // Every name the app ever asks for, so an unknown one - almost
        // always a typo - can be reported rather than silently unused.
        // Per-control variants ("button.<id>", "icon.<id>") are checked
        // against the component IDs in SkinSpriteNames.h.
        static bool isKnownName(const juce::String& name);

    private:
        const juce::Image& pickImage(const SpriteItem& item, const juce::Graphics& g) const;

        std::map<juce::String, SpriteItem> items;
        float scale = 1.0f;
        bool pixelArt = true;
        bool hasSheet2x = false;
    };

    // The sprites in use. Like the palette in InkwyrdTheme.h:
    // MESSAGE THREAD ONLY - set by applySkin(), read by painting.
    const SkinSprites& activeSprites();
    void setActiveSprites(SkinSprites sprites);
}
