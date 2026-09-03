#include "SoundboardGridComponent.h"

namespace
{
    constexpr int kMinCellWidth = 120;
    constexpr int kCellHeight = 72;
    constexpr int kCellGap = 6;
    constexpr int kCaptionHeight = 22;
}

SoundboardGridComponent::SoundboardGridComponent(SoundboardEngine& soundboardToUse)
    : soundboard(soundboardToUse)
{
    addAndMakeVisible(caption);

    emptyMessage.setText("No sounds yet - pick a soundboard folder in Settings.",
                          juce::dontSendNotification);
    emptyMessage.setColour(juce::Label::textColourId, juce::Colours::grey);
    emptyMessage.setJustificationType(juce::Justification::centredTop);
    addAndMakeVisible(emptyMessage);

    viewport.setViewedComponent(&gridPanel, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
}

void SoundboardGridComponent::setSoundNames(const juce::StringArray& names)
{
    soundNames = names;
    rebuildButtons();
    resized();
}

void SoundboardGridComponent::rebuildButtons()
{
    buttons.clear();

    for (const auto& name : soundNames)
    {
        auto* button = buttons.add(new juce::TextButton(name));
        gridPanel.addAndMakeVisible(button);
        button->onClick = [this, name]
        {
            // Guarded rather than assumed: a name can go stale if the
            // soundboard folder changed under us.
            if (soundboard.hasSound(name))
                soundboard.trigger(name);
        };
    }

    emptyMessage.setVisible(soundNames.isEmpty());
    viewport.setVisible(!soundNames.isEmpty());
}

void SoundboardGridComponent::resized()
{
    auto area = getLocalBounds();
    caption.setBounds(area.removeFromTop(kCaptionHeight));
    area.removeFromTop(4);

    if (soundNames.isEmpty())
    {
        emptyMessage.setBounds(area.removeFromTop(40));
        return;
    }

    viewport.setBounds(area);

    auto usableWidth = juce::jmax(kMinCellWidth, area.getWidth() - viewport.getScrollBarThickness());
    auto columns = juce::jmax(1, (usableWidth + kCellGap) / (kMinCellWidth + kCellGap));
    auto cellWidth = (usableWidth - (columns - 1) * kCellGap) / columns;

    int rows = (buttons.size() + columns - 1) / columns;
    gridPanel.setSize(usableWidth, juce::jmax(kCellHeight, rows * (kCellHeight + kCellGap)));

    for (int i = 0; i < buttons.size(); ++i)
    {
        int column = i % columns;
        int row = i / columns;
        buttons[i]->setBounds(column * (cellWidth + kCellGap),
                               row * (kCellHeight + kCellGap),
                               cellWidth,
                               kCellHeight);
    }
}
