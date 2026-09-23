#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "InkwyrdTheme.h"

// The black-and-dark-green skin, implemented from the design mockup.
//
// A LookAndFeel rather than per-component painting wherever possible:
// every juce::TextButton, Slider, ListBox, ScrollBar and TextEditor in
// the app picks this up without being touched, which is what makes a
// skin this size tractable. Components that already draw themselves
// (soundboard pads, playlist rows, volume bars) read the same palette
// from InkwyrdTheme.h so they can't drift.
//
// SPRITES. A skin can also supply per-widget images (SkinSprites.h). Every
// method below checks for one first and falls back to drawing in code, so
// a skin with only a few sprites still paints everything else exactly as
// before - and the built-in look, which has none, is untouched.
//
// TITLE BARS ARE OURS NOW. The five layout windows used to use native
// Windows captions; the mockup's title bar - logo, "INKWYRD" over a
// subtitle, custom controls - can't be drawn on one, since Windows only
// exposes its colour. So DetachableWindow turns the native title bar off
// and this class draws it. See DetachableWindow.h for what that changed
// about dragging and snapping, which is a bigger deal than it sounds.
class InkwyrdLookAndFeel : public juce::LookAndFeel_V4
{
public:
    InkwyrdLookAndFeel();

    // Re-copies the palette into JUCE's colour IDs. Call after applying a
    // skin, then repaint - see InkwyrdAudioApplication::applySkin().
    void refreshColours();

    // A window can put a second line under "INKWYRD" - "AUDIO LIBRARY",
    // "AUDIO PLAYER", and so on. Implemented as an interface the window
    // provides rather than a setter here, because the LookAndFeel is
    // shared by every window and must stay stateless about any of them.
    struct TitleBarInfo
    {
        virtual ~TitleBarInfo() = default;
        virtual juce::String getTitleBarSubtitle() const = 0;
    };

    void drawDocumentWindowTitleBar(juce::DocumentWindow& window, juce::Graphics& g,
                                     int w, int h, int titleSpaceX, int titleSpaceW,
                                     const juce::Image* icon, bool drawTitleTextOnLeft) override;

    juce::Button* createDocumentWindowButton(int buttonType) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override;

    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;

    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;

    // Only to recolour the sort arrow, which JUCE hard-codes as
    // translucent black - invisible on a dark header.
    void drawTableHeaderColumn(juce::Graphics& g, juce::TableHeaderComponent& header,
                                const juce::String& columnName, int columnId,
                                int width, int height, bool isMouseOver, bool isMouseDown,
                                int columnFlags) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override;

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;

    // Only so a skin's "window" sprite can sit behind everything.
    void fillResizableWindowBackground(juce::Graphics& g, int w, int h,
                                        const juce::BorderSize<int>& border,
                                        juce::ResizableWindow& window) override;

    // Panels and framed blocks, for the components that lay themselves
    // out by hand. Here rather than duplicated in five files.
    static void drawPanel(juce::Graphics& g, juce::Rectangle<int> area, bool raised = false);
    //
    // A skin with sprites can replace either: "panel" / "panel.raised",
    // and "well" - or a more specific name for one particular well (the
    // Player's screen asks for "display"), falling back to "well".
    static void drawInsetWell(juce::Graphics& g, juce::Rectangle<int> area,
                               const char* spriteName = "well");

    // The ink-bottle mark from the mockup, drawn as vectors.
    //
    // Deliberately an approximation and worth replacing: the real logo
    // is a piece of artwork, and redrawing someone's artwork from a
    // low-resolution screenshot gets the gesture but not the detail. If
    // the original SVG or a PNG turns up, this becomes a
    // juce::Drawable/ImageCache load and the shape below goes away.
    static void drawLogo(juce::Graphics& g, juce::Rectangle<float> area,
                          juce::Colour ink, juce::Colour glow);

    // A skin can replace the drawn mark with its own image (PNG, JPEG,
    // GIF or SVG). Pass {} to go back to the drawn one. Returns false and
    // fills errorMessage if the file couldn't be used - the drawn mark
    // stays, so a bad logo is never a blank title bar.
    static bool setSkinLogo(const juce::File& file, juce::String& errorMessage);
    static bool hasSkinLogo();

    static juce::Font titleFont(float height);
    static juce::Font labelFont(float height);
    static juce::Font digitFont(float height);
};
