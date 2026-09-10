#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PlaylistEngine.h"
#include "SpectrumTap.h"

// The "digital screen" from the design: art slot, artist/title/time
// readout, spectrum, and a seek bar you can drag.
//
// Drawn entirely in code - there are no image assets anywhere in it. The
// one thing that IS artwork, the ink-bottle mark, is a vector
// approximation in InkwyrdLookAndFeel::drawLogo() and is meant to be
// replaced by the real thing.
//
// Repaints on a timer rather than from the audio thread. A visualiser
// that repainted per audio block would ask the message thread for ~90
// repaints a second and drop most of them anyway.
class NowPlayingDisplay : public juce::Component,
                           private juce::Timer
{
public:
    NowPlayingDisplay(PlaylistEngine& engineToUse, SpectrumTap& spectrumToUse);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void seekTo(const juce::MouseEvent& e);

    void paintArtSlot(juce::Graphics& g, juce::Rectangle<int> area);
    void paintTrackInfo(juce::Graphics& g, juce::Rectangle<int> area);
    void paintSpectrum(juce::Graphics& g, juce::Rectangle<int> area);
    void paintSeekBar(juce::Graphics& g, juce::Rectangle<int> area);

    PlaylistEngine& engine;
    SpectrumTap& spectrum;

    juce::Rectangle<int> artArea, infoArea, spectrumArea, seekArea;

    std::array<float, SpectrumTap::numBands> bands {};
    bool haveBands = false;

    // Drives the glow behind the mark. Follows the loudest thing in the
    // spectrum rather than a separate level meter, so it breathes with
    // the music without needing its own tap.
    float glow = 0.0f;

    // While the user is dragging the seek bar, the display follows the
    // MOUSE rather than the engine - otherwise the handle fights the
    // cursor, snapping back to the real position between drag events.
    bool scrubbing = false;
    double scrubProportion = 0.0;
};
