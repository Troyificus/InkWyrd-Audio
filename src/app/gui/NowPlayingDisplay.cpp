#include "NowPlayingDisplay.h"

#include "InkwyrdLookAndFeel.h"
#include "InkwyrdTheme.h"
#include "TagEditor.h"

using namespace inkwyrd::theme;

namespace
{
    constexpr int kRefreshHz = 30;
    constexpr int kSeekBarHeight = 18;
    constexpr int kGap = 12;

    juce::String formatTime(double seconds)
    {
        if (seconds < 0.0 || ! std::isfinite(seconds))
            seconds = 0.0;

        auto total = (int) seconds;
        return juce::String::formatted("%02d:%02d", total / 60, total % 60);
    }

    struct TrackName
    {
        juce::String artist, title;
    };

    // The fallback when a file has no title tag, or hasn't been scanned
    // yet: filenames are very often "Artist - Title", so splitting on the
    // first " - " still gets a real artist line a lot of the time.
    TrackName splitTrackName(const juce::File& file)
    {
        auto name = file.getFileNameWithoutExtension();
        auto separator = name.indexOf(" - ");

        if (separator > 0)
            return { name.substring(0, separator).trim(), name.substring(separator + 3).trim() };

        return { {}, name };
    }

    // Tags first - the same source as the Library and Playlist windows,
    // so the three never disagree about what a track is called. Each
    // field falls back on its own: a file tagged with an artist but no
    // title still gets its artist.
    TrackName trackNameFor(const juce::File& file, const TrackMetadataStore& trackMetadata)
    {
        if (file == juce::File())
            return {};

        auto metadata = trackMetadata.get(file);
        auto fromFilename = splitTrackName(file);

        if (metadata.title.isNotEmpty())
            return { metadata.artist, metadata.title };

        return { metadata.artist.isNotEmpty() ? metadata.artist : fromFilename.artist,
                  fromFilename.title };
    }

    void drawCaption(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
    {
        g.setColour(textDim);
        g.setFont(InkwyrdLookAndFeel::labelFont(11.0f).withExtraKerningFactor(0.18f));
        g.drawText(text.toUpperCase(), area, juce::Justification::centredLeft, false);
    }
}

NowPlayingDisplay::NowPlayingDisplay(PlaylistEngine& engineToUse, SpectrumTap& spectrumToUse,
                                      const TrackMetadataStore& trackMetadataToUse)
    : engine(engineToUse), spectrum(spectrumToUse), trackMetadata(trackMetadataToUse)
{
    startTimerHz(kRefreshHz);
}

void NowPlayingDisplay::refreshArtworkIfTrackChanged()
{
    auto file = engine.getCurrentTrackFile();
    if (file == artworkFile)
        return;

    artworkFile = file;
    artwork = {};

    if (file == juce::File())
        return;

    auto tags = inkwyrd::TagEditor::read(file);
    if (tags.artwork.getSize() > 0)
        artwork = juce::ImageFileFormat::loadFrom(tags.artwork.getData(), tags.artwork.getSize());
}

void NowPlayingDisplay::timerCallback()
{
    refreshArtworkIfTrackChanged();

    haveBands = spectrum.readBands(bands);

    if (haveBands)
    {
        float peak = 0.0f;
        for (auto band : bands)
            peak = juce::jmax(peak, band);

        // Rises with the music, falls back slowly, so the glow pulses
        // rather than flickering on every transient.
        glow = peak > glow ? peak : glow * 0.9f;
    }
    else
    {
        glow *= 0.9f;
    }

    repaint();
}

void NowPlayingDisplay::resized()
{
    auto area = getLocalBounds().reduced(kGap);

    seekArea = area.removeFromBottom(kSeekBarHeight);
    area.removeFromBottom(10);
    spectrumArea = area.removeFromBottom(juce::jlimit(28, 44, area.getHeight() / 3));
    area.removeFromBottom(12);

    // Square art slot on the left, capped so it can't eat the info
    // block on a wide window.
    auto artSize = juce::jlimit(56, 120, juce::jmin(area.getHeight(), area.getWidth() / 3));
    artArea = area.removeFromLeft(artSize).withHeight(artSize);
    area.removeFromLeft(kGap + 6);
    infoArea = area;
}

void NowPlayingDisplay::paint(juce::Graphics& g)
{
    InkwyrdLookAndFeel::drawInsetWell(g, getLocalBounds());

    paintArtSlot(g, artArea);
    paintTrackInfo(g, infoArea);
    paintSpectrum(g, spectrumArea);
    paintSeekBar(g, seekArea);
}

void NowPlayingDisplay::paintArtSlot(juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty())
        return;

    auto slot = area.toFloat();

    g.setColour(panelDeep);
    g.fillRoundedRectangle(slot, cornerRadius);

    // The glow is drawn HERE rather than baked into the mark, as
    // concentric fading rings. That's what lets it react to the audio -
    // and it's also why the real logo artwork, when it arrives, should
    // arrive WITHOUT a glow: JUCE's SVG renderer ignores blur filters
    // anyway, so a baked one would silently vanish.
    auto centre = slot.getCentre();
    auto maxRadius = slot.getWidth() * 0.46f;

    for (int ring = 5; ring >= 1; --ring)
    {
        auto t = (float) ring / 5.0f;
        auto radius = maxRadius * (0.55f + 0.45f * t) * (0.85f + 0.3f * glow);
        g.setColour(accent.withAlpha(0.05f * glow * (1.0f - t) + 0.015f));
        g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));
    }

    if (artwork.isValid())
    {
        // The track's own cover art, clipped to the slot's rounded
        // corners so it sits in the panel rather than on top of it.
        juce::Path rounded;
        rounded.addRoundedRectangle(slot.reduced(1.0f), cornerRadius);

        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(rounded);
        g.drawImage(artwork, slot.reduced(1.0f), juce::RectanglePlacement::centred
                                                   | juce::RectanglePlacement::fillDestination);
    }
    else
    {
        InkwyrdLookAndFeel::drawLogo(g, slot.reduced(slot.getWidth() * 0.22f),
                                      accent.withMultipliedBrightness(0.9f + 0.3f * glow),
                                      accentSoft.withAlpha(0.35f + 0.25f * glow));
    }

    g.setColour(outline.withAlpha(0.7f));
    g.drawRoundedRectangle(slot.reduced(0.5f), cornerRadius, 1.0f);
}

void NowPlayingDisplay::paintTrackInfo(juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty())
        return;

    // Looked up every paint rather than cached per track: it's one map
    // lookup, and it means tags that arrive from a scan mid-track show up
    // without anything having to tell this display about them.
    auto name = trackNameFor(engine.getCurrentTrackFile(), trackMetadata);

    // FIXED row heights, not fractions of the available space. Deriving
    // them from the height meant a font sized at 1.5x its own row, which
    // overflowed upward and drew the time readout straight through the
    // "TIME" caption above it.
    constexpr int kCaptionRow = 12;
    constexpr int kArtistRow = 18;
    constexpr int kTitleRow = 24;
    constexpr int kTimeRow = 28;

    drawCaption(g, area.removeFromTop(kCaptionRow), "Artist");

    // Same family and colour as the title below, a size down: they are
    // two halves of one readout, and two fonts made it look like two
    // unrelated pieces of text.
    g.setColour(accent);
    g.setFont(InkwyrdLookAndFeel::titleFont(15.0f));
    g.drawText(name.artist.isNotEmpty() ? name.artist : juce::String("--"),
                area.removeFromTop(kArtistRow), juce::Justification::centredLeft, true);

    area.removeFromTop(2);

    // The crossfade marker rides on the caption rather than being
    // appended to the title, which would push the actual track name out
    // of view on exactly the tracks you most want to read.
    drawCaption(g, area.removeFromTop(kCaptionRow),
                 engine.isCrossfading() ? "Title  -  crossfading" : "Title");
    g.setColour(accent);
    g.setFont(InkwyrdLookAndFeel::titleFont(19.0f));
    g.drawText(name.title.isNotEmpty() ? name.title : juce::String("Nothing playing"),
                area.removeFromTop(kTitleRow), juce::Justification::centredLeft, true);

    area.removeFromTop(2);

    drawCaption(g, area.removeFromTop(kCaptionRow), "Time");

    auto length = engine.getCurrentTrackLengthSeconds();
    auto position = scrubbing ? scrubProportion * length : engine.getCurrentPositionSeconds();

    auto timeArea = area.removeFromTop(kTimeRow);
    auto timeText = formatTime(position) + "  /  " + formatTime(length);

    // A faint copy behind the text, offset by nothing but blurred by
    // being drawn larger and translucent - enough to read as a lit
    // display rather than flat text, without a real blur.
    g.setColour(accent.withAlpha(0.25f));
    g.setFont(InkwyrdLookAndFeel::digitFont(23.0f));
    g.drawText(timeText, timeArea.expanded(1), juce::Justification::centredLeft, false);

    g.setColour(accent);
    g.setFont(InkwyrdLookAndFeel::digitFont(23.0f));
    g.drawText(timeText, timeArea, juce::Justification::centredLeft, false);
}

void NowPlayingDisplay::paintSpectrum(juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty())
        return;

    auto bandCount = (int) bands.size();
    auto barWidth = (float) area.getWidth() / (float) bandCount;
    auto gap = juce::jmax(1.0f, barWidth * 0.25f);

    for (int i = 0; i < bandCount; ++i)
    {
        auto level = haveBands ? bands[(size_t) i] : 0.0f;

        // A floor, so an idle display reads as a row of dim segments
        // rather than an empty box that looks broken.
        auto height = juce::jmax(2.0f, (float) area.getHeight() * level);

        juce::Rectangle<float> bar(area.getX() + (float) i * barWidth,
                                    (float) area.getBottom() - height,
                                    barWidth - gap,
                                    height);

        g.setColour(accent.withAlpha(0.35f + 0.65f * level));
        g.fillRect(bar);
    }
}

void NowPlayingDisplay::paintSeekBar(juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty())
        return;

    auto length = engine.getCurrentTrackLengthSeconds();
    auto proportion = scrubbing ? scrubProportion
                                : (length > 0.0 ? engine.getCurrentPositionSeconds() / length : 0.0);
    proportion = juce::jlimit(0.0, 1.0, proportion);

    auto centreY = (float) area.getCentreY();
    juce::Rectangle<float> track((float) area.getX(), centreY - 2.0f, (float) area.getWidth(), 4.0f);

    g.setColour(panelDeep);
    g.fillRoundedRectangle(track, 2.0f);

    g.setColour(length > 0.0 ? accent : outline);
    g.fillRoundedRectangle(track.withWidth(track.getWidth() * (float) proportion), 2.0f);

    if (length > 0.0)
    {
        auto handleX = track.getX() + track.getWidth() * (float) proportion;
        constexpr float radius = 7.0f;

        g.setColour(accent);
        g.fillEllipse(handleX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(background);
        g.fillEllipse(handleX - radius * 0.4f, centreY - radius * 0.4f, radius * 0.8f, radius * 0.8f);
    }
}

void NowPlayingDisplay::mouseDown(const juce::MouseEvent& e)
{
    if (! seekArea.expanded(0, 8).contains(e.getPosition()) || engine.getCurrentTrackLengthSeconds() <= 0.0)
        return;

    scrubbing = true;
    seekTo(e);
}

void NowPlayingDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (scrubbing)
        seekTo(e);
}

void NowPlayingDisplay::mouseUp(const juce::MouseEvent& e)
{
    if (! scrubbing)
        return;

    // Committed on release as well as during the drag, so letting go
    // always lands on exactly what the handle was showing.
    seekTo(e);
    scrubbing = false;
}

void NowPlayingDisplay::seekTo(const juce::MouseEvent& e)
{
    if (seekArea.getWidth() <= 0)
        return;

    scrubProportion = juce::jlimit(0.0, 1.0,
                                    (double) (e.x - seekArea.getX()) / (double) seekArea.getWidth());

    engine.setPositionSeconds(scrubProportion * engine.getCurrentTrackLengthSeconds());
    repaint();
}
