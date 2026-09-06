#include "NowPlayingTrackListComponent.h"

namespace
{
    constexpr int kRowHeight = 24;
}

class NowPlayingTrackListComponent::Model : public juce::ListBoxModel
{
public:
    explicit Model(NowPlayingTrackListComponent& ownerToUse) : owner(ownerToUse) {}

    int getNumRows() override { return owner.engine.getPlayOrder().size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        auto& order = owner.engine.getPlayOrder();
        if (!juce::isPositiveAndBelow(row, order.size()))
            return;

        if (selected)
            g.fillAll(juce::Colours::white.withAlpha(0.12f));

        auto file = order[row];
        auto playing = file == owner.engine.getCurrentTrackFile();
        g.setColour(playing ? juce::Colours::lightgreen : juce::Colours::white);
        g.drawText((playing ? juce::String::fromUTF8("\xe2\x96\xb6 ") : juce::String("   "))
                        + file.getFileNameWithoutExtension(),
                    juce::Rectangle<int>(6, 0, width - 12, height), juce::Justification::centredLeft, true);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        auto& order = owner.engine.getPlayOrder();
        if (juce::isPositiveAndBelow(row, order.size()))
            owner.engine.crossfadeToTrackInCurrentList(order[row]);
    }

private:
    NowPlayingTrackListComponent& owner;
};

NowPlayingTrackListComponent::NowPlayingTrackListComponent(PlaylistEngine& engineToUse)
    : engine(engineToUse)
{
    captionLabel.setText("Playlist", juce::dontSendNotification);
    captionLabel.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    addAndMakeVisible(captionLabel);

    model = std::make_unique<Model>(*this);
    trackListBox.setModel(model.get());
    trackListBox.setRowHeight(kRowHeight);
    addAndMakeVisible(trackListBox);

    startTimer(500); // matches PlayerComponent's own now-playing poll cadence
}

NowPlayingTrackListComponent::~NowPlayingTrackListComponent() = default;

void NowPlayingTrackListComponent::setPlayingPlaylistName(const juce::String& name)
{
    captionLabel.setText(name.isNotEmpty() ? name : juce::String("Playlist"), juce::dontSendNotification);
}

void NowPlayingTrackListComponent::timerCallback()
{
    auto& order = engine.getPlayOrder();
    auto current = engine.getCurrentTrackFile();

    if (order != lastSeenOrder)
    {
        // The row count and/or its contents changed (a different
        // playlist was activated, or the order reshuffled) - a full
        // updateContent() is needed, not just a repaint.
        lastSeenOrder = order;
        lastSeenCurrent = current;
        trackListBox.updateContent();
        trackListBox.repaint();
    }
    else if (current != lastSeenCurrent)
    {
        // Same list, just moved to a different track in it.
        lastSeenCurrent = current;
        trackListBox.repaint();
    }
}

void NowPlayingTrackListComponent::resized()
{
    auto area = getLocalBounds().reduced(12);
    captionLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);
    trackListBox.setBounds(area);
}
