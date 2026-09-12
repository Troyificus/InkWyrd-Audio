#include "InkwyrdLookAndFeel.h"

using namespace inkwyrd::theme;

namespace
{
    // The mockup's headings are a geometric sans with generous letter
    // spacing; its numbers are monospaced. Nothing is bundled - a font
    // is a licensing decision, not a styling one - so these pick the
    // closest thing Windows already has and fall back to JUCE's default
    // sans anywhere else.
    juce::Font makeFont(const juce::String& preferred, float height, int styleFlags)
    {
        juce::Font font(juce::FontOptions(preferred, height, styleFlags));

        if (font.getTypefaceName() != preferred)
            font = juce::Font(juce::FontOptions(height, styleFlags));

        return font;
    }

    // Title-bar buttons: the mockup's are thin dark glyphs on the green
    // bar, not JUCE's default coloured circles.
    class TitleBarButton : public juce::Button
    {
    public:
        enum class Kind { minimise, maximise, close };

        explicit TitleBarButton(Kind kindToUse)
            : juce::Button({}), kind(kindToUse) {}

        void paintButton(juce::Graphics& g, bool isOver, bool isDown) override
        {
            auto area = getLocalBounds().toFloat();

            if (isOver || isDown)
            {
                // Close gets a red wash on hover, the way every window
                // does; the others just darken the bar under them.
                auto wash = kind == Kind::close ? danger.withAlpha(isDown ? 0.9f : 0.75f)
                                                 : titleBarText.withAlpha(isDown ? 0.22f : 0.12f);
                g.setColour(wash);
                g.fillRect(area);
            }

            auto glyph = area.withSizeKeepingCentre(10.0f, 10.0f);
            g.setColour(kind == Kind::close && (isOver || isDown) ? titleBar : titleBarText);

            switch (kind)
            {
                case Kind::minimise:
                    g.fillRect(glyph.getX(), glyph.getCentreY() - 0.5f, glyph.getWidth(), 1.4f);
                    break;

                case Kind::maximise:
                    g.drawRect(glyph, 1.4f);
                    break;

                case Kind::close:
                    g.drawLine(glyph.getX(), glyph.getY(), glyph.getRight(), glyph.getBottom(), 1.4f);
                    g.drawLine(glyph.getX(), glyph.getBottom(), glyph.getRight(), glyph.getY(), 1.4f);
                    break;
            }
        }

    private:
        Kind kind;
    };
}

InkwyrdLookAndFeel::InkwyrdLookAndFeel()
{
    refreshColours();
}

void InkwyrdLookAndFeel::refreshColours()
{
    // The colour IDs that components read directly. Everything the app
    // creates without explicit colours lands on these.
    //
    // Called again whenever a skin is applied: these are COPIES of the
    // palette taken once, so changing inkwyrd::theme's values alone would
    // leave every stock JUCE widget on the old skin's colours.
    setColour(juce::ResizableWindow::backgroundColourId, panel);
    setColour(juce::DocumentWindow::textColourId, titleBarText);

    setColour(juce::Label::textColourId, text);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    setColour(juce::TextButton::buttonColourId, panelRaised);
    setColour(juce::TextButton::buttonOnColourId, accentSoft);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::textColourOnId, background);

    setColour(juce::TextEditor::backgroundColourId, panelDeep);
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::highlightColourId, accentSoft);
    setColour(juce::TextEditor::highlightedTextColourId, background);
    setColour(juce::TextEditor::outlineColourId, outline);
    setColour(juce::TextEditor::focusedOutlineColourId, accent);
    setColour(juce::CaretComponent::caretColourId, accent);

    setColour(juce::ListBox::backgroundColourId, panelDeep);
    setColour(juce::ListBox::textColourId, text);
    setColour(juce::ListBox::outlineColourId, outlineFaint);

    // Column headers on the Library and Playlist tables. Left to V4's
    // defaults these were a flat light grey - the one unskinned surface
    // left in either window.
    setColour(juce::TableHeaderComponent::backgroundColourId, panelRaised);
    setColour(juce::TableHeaderComponent::textColourId, text);
    setColour(juce::TableHeaderComponent::outlineColourId, outline);
    setColour(juce::TableHeaderComponent::highlightColourId, accentSoft.withAlpha(0.35f));

    // The Library's folder tree. JUCE fills a selected row itself from
    // selectedItemBackgroundColourId (see ItemComponent::paint), which is
    // why the items' own paintItem draws no selection.
    setColour(juce::TreeView::backgroundColourId, panelDeep);
    setColour(juce::TreeView::linesColourId, outlineFaint);
    setColour(juce::TreeView::selectedItemBackgroundColourId, accentSoft.withAlpha(0.35f));
    setColour(juce::TreeView::oddItemsColourId, juce::Colours::transparentBlack);
    setColour(juce::TreeView::evenItemsColourId, juce::Colours::transparentBlack);
    setColour(juce::TreeView::dragAndDropIndicatorColourId, accent);

    setColour(juce::ScrollBar::thumbColourId, accentSoft);
    setColour(juce::ScrollBar::trackColourId, panelDeep);

    setColour(juce::Slider::backgroundColourId, panelDeep);
    setColour(juce::Slider::thumbColourId, accent);
    setColour(juce::Slider::trackColourId, accentSoft);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxBackgroundColourId, panelDeep);
    setColour(juce::Slider::textBoxOutlineColourId, outline);

    setColour(juce::ComboBox::backgroundColourId, panelDeep);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::outlineColourId, outline);
    setColour(juce::ComboBox::arrowColourId, textDim);

    setColour(juce::PopupMenu::backgroundColourId, panelRaised);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accentSoft);
    setColour(juce::PopupMenu::highlightedTextColourId, background);

    setColour(juce::ToggleButton::textColourId, text);
    setColour(juce::ToggleButton::tickColourId, accent);
    setColour(juce::ToggleButton::tickDisabledColourId, outline);

    setColour(juce::AlertWindow::backgroundColourId, panel);
    setColour(juce::AlertWindow::textColourId, text);
    setColour(juce::AlertWindow::outlineColourId, outline);

    setColour(juce::TooltipWindow::backgroundColourId, panelRaised);
    setColour(juce::TooltipWindow::textColourId, text);
    setColour(juce::TooltipWindow::outlineColourId, outline);
}

juce::Font InkwyrdLookAndFeel::titleFont(float height)
{
    return makeFont(current().titleFontName, height, juce::Font::bold);
}

juce::Font InkwyrdLookAndFeel::labelFont(float height)
{
    return makeFont(current().labelFontName, height, juce::Font::plain);
}

juce::Font InkwyrdLookAndFeel::digitFont(float height)
{
    // Monospaced, for the readouts the mockup renders as a digital
    // display - times, track numbers, dB values. Their columns lining up
    // is most of what makes them read as a readout rather than as text.
    return makeFont(current().digitFontName, height, juce::Font::plain);
}

namespace
{
    // The skin's logo, loaded once. Static because drawLogo() is static -
    // it is called from the title bar of every window and from the
    // now-playing display, none of which have a LookAndFeel to hand.
    std::unique_ptr<juce::Drawable> skinLogo;

    // Enough for any sensible mark at any size this app draws one. A
    // 12,000px PNG in a shared skin would otherwise be decoded and
    // rescaled behind every title bar.
    constexpr int kMaxLogoPixels = 2048;
    constexpr juce::int64 kMaxLogoBytes = 8 * 1024 * 1024;
}

bool InkwyrdLookAndFeel::setSkinLogo(const juce::File& file, juce::String& errorMessage)
{
    skinLogo.reset();

    if (file == juce::File())
        return true; // no logo is not a failure - the drawn mark is used

    if (file.getSize() > kMaxLogoBytes)
    {
        errorMessage = "Logo is larger than 8 MB - using the drawn mark.";
        return false;
    }

    if (file.hasFileExtension("svg"))
    {
        // JUCE's SVG renderer ignores filters and blur and doesn't draw
        // text - a logo relying on those will come out plainer than it
        // looks in a browser. Shapes and paths are fine.
        if (auto parsed = juce::Drawable::createFromSVGFile(file))
        {
            skinLogo = std::move(parsed);
            return true;
        }

        errorMessage = "Couldn't read that SVG - using the drawn mark.";
        return false;
    }

    auto image = juce::ImageFileFormat::loadFrom(file);

    if (! image.isValid())
    {
        errorMessage = "Couldn't read that image - using the drawn mark.";
        return false;
    }

    if (image.getWidth() > kMaxLogoPixels || image.getHeight() > kMaxLogoPixels)
    {
        errorMessage = "Logo is bigger than " + juce::String(kMaxLogoPixels)
                        + " pixels - using the drawn mark.";
        return false;
    }

    auto drawable = std::make_unique<juce::DrawableImage>();
    drawable->setImage(image);
    skinLogo = std::move(drawable);
    return true;
}

bool InkwyrdLookAndFeel::hasSkinLogo() { return skinLogo != nullptr; }

void InkwyrdLookAndFeel::drawLogo(juce::Graphics& g, juce::Rectangle<float> area,
                                   juce::Colour ink, juce::Colour glow)
{
    // A skin's own artwork replaces the drawn mark entirely, colours and
    // all - recolouring someone's logo to the palette would wreck it.
    if (skinLogo != nullptr)
    {
        skinLogo->drawWithin(g, area, juce::RectanglePlacement::centred, 1.0f);
        return;
    }

    auto bottle = area.reduced(area.getWidth() * 0.14f, area.getHeight() * 0.08f);

    // Neck and cap sit on top of a rounded body - enough to read as an
    // ink bottle at 24px, which is the only size it appears at.
    auto capHeight = bottle.getHeight() * 0.16f;
    auto neckHeight = bottle.getHeight() * 0.10f;

    auto cap = bottle.removeFromTop(capHeight).reduced(bottle.getWidth() * 0.28f, 0.0f);
    auto neck = bottle.removeFromTop(neckHeight).reduced(bottle.getWidth() * 0.34f, 0.0f);
    auto body = bottle;

    g.setColour(glow);
    g.fillRoundedRectangle(body, body.getWidth() * 0.22f);

    g.setColour(ink);
    g.drawRoundedRectangle(body, body.getWidth() * 0.22f, 1.4f);
    g.fillRect(neck);
    g.fillRoundedRectangle(cap, capHeight * 0.35f);

    g.setFont(titleFont(body.getHeight() * 0.62f));
    g.drawText("W", body, juce::Justification::centred, false);
}

void InkwyrdLookAndFeel::drawDocumentWindowTitleBar(juce::DocumentWindow& window, juce::Graphics& g,
                                                      int w, int h, int titleSpaceX, int titleSpaceW,
                                                      const juce::Image*, bool)
{
    juce::ignoreUnused(titleSpaceX, titleSpaceW);

    juce::Rectangle<float> bar(0.0f, 0.0f, (float) w, (float) h);

    // Rounded at the top only - the bar meets the window body squarely.
    juce::Path shape;
    shape.addRoundedRectangle(bar.getX(), bar.getY(), bar.getWidth(), bar.getHeight() + cornerRadius,
                               cornerRadius, cornerRadius, true, true, false, false);
    g.setColour(titleBar);
    g.fillPath(shape);

    auto content = bar.reduced(10.0f, 6.0f);

    auto logoArea = content.removeFromLeft(content.getHeight());
    drawLogo(g, logoArea, titleBarText, titleBar.darker(0.25f));

    content.removeFromLeft(10.0f);

    // "INKWYRD" over its subtitle. When there's no subtitle the name is
    // centred vertically instead, so a window without one doesn't look
    // like it lost something.
    juce::String subtitle;
    if (auto* info = dynamic_cast<TitleBarInfo*>(&window))
        subtitle = info->getTitleBarSubtitle();

    g.setColour(titleBarText);

    if (subtitle.isNotEmpty())
    {
        auto nameRow = content.removeFromTop(content.getHeight() * 0.58f);
        g.setFont(titleFont(nameRow.getHeight() * 0.95f).withExtraKerningFactor(0.09f));
        g.drawText("INKWYRD", nameRow, juce::Justification::centredLeft, false);

        g.setColour(titleBarSubtle);
        g.setFont(labelFont(content.getHeight() * 0.82f).withExtraKerningFactor(0.16f));
        g.drawText(subtitle.toUpperCase(), content, juce::Justification::centredLeft, false);
    }
    else
    {
        g.setFont(titleFont(content.getHeight() * 0.5f).withExtraKerningFactor(0.09f));
        g.drawText("INKWYRD", content, juce::Justification::centredLeft, false);
    }
}

juce::Button* InkwyrdLookAndFeel::createDocumentWindowButton(int buttonType)
{
    if (buttonType == juce::DocumentWindow::minimiseButton)
        return new TitleBarButton(TitleBarButton::Kind::minimise);
    if (buttonType == juce::DocumentWindow::maximiseButton)
        return new TitleBarButton(TitleBarButton::Kind::maximise);

    return new TitleBarButton(TitleBarButton::Kind::close);
}

void InkwyrdLookAndFeel::drawPanel(juce::Graphics& g, juce::Rectangle<int> area, bool raised)
{
    auto r = area.toFloat();
    g.setColour(raised ? panelRaised : panelDeep);
    g.fillRoundedRectangle(r, cornerRadius);
    g.setColour(outlineFaint);
    g.drawRoundedRectangle(r.reduced(0.5f), cornerRadius, 1.0f);
}

void InkwyrdLookAndFeel::drawInsetWell(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat();
    g.setColour(background);
    g.fillRoundedRectangle(r, cornerRadius);
    g.setColour(outline.withAlpha(0.6f));
    g.drawRoundedRectangle(r.reduced(0.5f), cornerRadius, 1.0f);
}

void InkwyrdLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& backgroundColour,
                                               bool shouldDrawButtonAsHighlighted,
                                               bool shouldDrawButtonAsDown)
{
    auto area = button.getLocalBounds().toFloat().reduced(0.5f);
    auto on = button.getToggleState();

    auto fill = on ? accentSoft : backgroundColour;
    if (shouldDrawButtonAsDown)      fill = fill.brighter(0.25f);
    else if (shouldDrawButtonAsHighlighted) fill = fill.brighter(0.12f);

    g.setColour(fill);
    g.fillRoundedRectangle(area, cornerRadius);

    g.setColour(on ? accent : outline);
    g.drawRoundedRectangle(area, cornerRadius, on ? 1.4f : 1.0f);
}

void InkwyrdLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                         bool, bool)
{
    g.setFont(labelFont((float) juce::jmin(15, button.getHeight() - 10)));
    g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                           : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.4f));

    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(8, 2),
                      juce::Justification::centred, 2);
}

void InkwyrdLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                           bool shouldDrawButtonAsHighlighted, bool)
{
    auto area = button.getLocalBounds();
    auto boxSize = juce::jmin(18, area.getHeight() - 2);
    auto box = area.removeFromLeft(boxSize + 6).withSizeKeepingCentre(boxSize, boxSize).toFloat();

    g.setColour(button.getToggleState() ? accentSoft : panelDeep);
    g.fillRoundedRectangle(box, 3.0f);

    g.setColour(button.getToggleState() ? accent
                                         : (shouldDrawButtonAsHighlighted ? textDim : outline));
    g.drawRoundedRectangle(box.reduced(0.5f), 3.0f, 1.2f);

    if (button.getToggleState())
    {
        // A tick, drawn rather than glyphed so it keeps its weight at
        // any size.
        juce::Path tick;
        tick.startNewSubPath(box.getX() + box.getWidth() * 0.24f, box.getCentreY());
        tick.lineTo(box.getCentreX() - box.getWidth() * 0.02f, box.getBottom() - box.getHeight() * 0.28f);
        tick.lineTo(box.getRight() - box.getWidth() * 0.2f, box.getY() + box.getHeight() * 0.28f);

        g.setColour(accent);
        g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    g.setColour(button.findColour(juce::ToggleButton::textColourId)
                     .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.4f));
    g.setFont(labelFont((float) juce::jmin(15, button.getHeight() - 6)));
    g.drawFittedText(button.getButtonText(), area.reduced(4, 0),
                      juce::Justification::centredLeft, 2);
}

void InkwyrdLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float, float,
                                           juce::Slider::SliderStyle style, juce::Slider& slider)
{
    auto isHorizontal = style == juce::Slider::LinearHorizontal
                         || style == juce::Slider::LinearBar;

    if (! isHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
        return;
    }

    auto centreY = (float) y + (float) height * 0.5f;
    constexpr float trackThickness = 4.0f;

    juce::Rectangle<float> track((float) x, centreY - trackThickness * 0.5f,
                                  (float) width, trackThickness);

    g.setColour(panelDeep);
    g.fillRoundedRectangle(track, trackThickness * 0.5f);

    auto filled = track.withRight(sliderPos);
    g.setColour(slider.isEnabled() ? accent : outline);
    g.fillRoundedRectangle(filled, trackThickness * 0.5f);

    auto knobRadius = juce::jmin(9.0f, (float) height * 0.42f);
    g.setColour(slider.isEnabled() ? accent : outline);
    g.fillEllipse(sliderPos - knobRadius, centreY - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);

    // A dark core, so the knob reads as a ring against a filled track
    // rather than merging into it.
    g.setColour(background);
    g.fillEllipse(sliderPos - knobRadius * 0.45f, centreY - knobRadius * 0.45f,
                   knobRadius * 0.9f, knobRadius * 0.9f);
}

void InkwyrdLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height,
                                                    juce::TextEditor& editor)
{
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height),
                            cornerRadius);
}

void InkwyrdLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height,
                                                juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    g.setColour(editor.hasKeyboardFocus(true)
                     ? editor.findColour(juce::TextEditor::focusedOutlineColourId)
                     : editor.findColour(juce::TextEditor::outlineColourId));

    g.drawRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height).reduced(0.5f),
                            cornerRadius, 1.0f);
}

void InkwyrdLookAndFeel::drawTableHeaderColumn(juce::Graphics& g, juce::TableHeaderComponent& header,
                                                const juce::String& columnName, int,
                                                int width, int height, bool isMouseOver, bool isMouseDown,
                                                int columnFlags)
{
    // LookAndFeel_V2's version, line for line, except the arrow colour.
    auto highlightColour = header.findColour(juce::TableHeaderComponent::highlightColourId);

    if (isMouseDown)
        g.fillAll(highlightColour);
    else if (isMouseOver)
        g.fillAll(highlightColour.withMultipliedAlpha(0.625f));

    juce::Rectangle<int> area(width, height);
    area.reduce(4, 0);

    if ((columnFlags & (juce::TableHeaderComponent::sortedForwards
                         | juce::TableHeaderComponent::sortedBackwards)) != 0)
    {
        juce::Path sortArrow;
        sortArrow.addTriangle(0.0f, 0.0f,
                               0.5f, (columnFlags & juce::TableHeaderComponent::sortedForwards) != 0 ? -0.8f : 0.8f,
                               1.0f, 0.0f);

        g.setColour(accent);
        g.fillPath(sortArrow, sortArrow.getTransformToScaleToFit(
                                   area.removeFromRight(height / 2).reduced(2).toFloat(), true));
    }

    g.setColour(header.findColour(juce::TableHeaderComponent::textColourId));
    g.setFont(withDefaultMetrics(juce::FontOptions((float) height * 0.5f, juce::Font::bold)));
    g.drawFittedText(columnName, area, juce::Justification::centredLeft, 1);
}

void InkwyrdLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&,
                                        int x, int y, int width, int height,
                                        bool isScrollbarVertical, int thumbStartPosition,
                                        int thumbSize, bool isMouseOver, bool isMouseDown)
{
    g.setColour(panelDeep);
    g.fillRect(x, y, width, height);

    if (thumbSize <= 0)
        return;

    juce::Rectangle<int> thumb = isScrollbarVertical
        ? juce::Rectangle<int>(x + 2, thumbStartPosition, width - 4, thumbSize)
        : juce::Rectangle<int>(thumbStartPosition, y + 2, thumbSize, height - 4);

    g.setColour(isMouseDown ? accent : (isMouseOver ? accentSoft.brighter(0.2f) : accentSoft));
    g.fillRoundedRectangle(thumb.toFloat(), (float) juce::jmin(thumb.getWidth(), thumb.getHeight()) * 0.5f);
}

void InkwyrdLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                       int, int, int, int, juce::ComboBox& box)
{
    juce::Rectangle<float> area(0.0f, 0.0f, (float) width, (float) height);

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(area, cornerRadius);

    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(area.reduced(0.5f), cornerRadius, 1.0f);

    juce::Path arrow;
    auto centre = juce::Point<float>((float) width - 14.0f, (float) height * 0.5f);
    arrow.addTriangle(centre.x - 5.0f, centre.y - 2.5f,
                       centre.x + 5.0f, centre.y - 2.5f,
                       centre.x,        centre.y + 3.5f);

    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}

void InkwyrdLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    juce::Rectangle<float> area(0.0f, 0.0f, (float) width, (float) height);
    g.setColour(panelRaised);
    g.fillRoundedRectangle(area, cornerRadius);
    g.setColour(outline);
    g.drawRoundedRectangle(area.reduced(0.5f), cornerRadius, 1.0f);
}
