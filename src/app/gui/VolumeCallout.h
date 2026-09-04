#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

// The little panel a volume bar expands into: a dB slider and a Reset.
//
// Shared by the soundboard buttons and the playlist's track list so the
// two behave identically - both are "this one clip is louder than the
// rest, pull it down", and it would be odd for them to look or feel
// different.
class VolumeCallout final : public juce::Component
{
public:
    VolumeCallout(const juce::String& titleText,
                   float initialDb,
                   float minDb,
                   float maxDb,
                   std::function<void(float)> onChanged)
        : onChange(std::move(onChanged))
    {
        title.setText(titleText, juce::dontSendNotification);
        title.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        addAndMakeVisible(title);

        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 20);
        slider.setRange(minDb, maxDb, 0.5);
        slider.setValue(initialDb, juce::dontSendNotification);
        slider.setTextValueSuffix(" dB");
        slider.onValueChange = [this] { if (onChange) onChange((float) slider.getValue()); };
        addAndMakeVisible(slider);

        resetButton.onClick = [this] { slider.setValue(0.0, juce::sendNotificationSync); };
        addAndMakeVisible(resetButton);

        setSize(300, 74);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        title.setBounds(area.removeFromTop(18));
        area.removeFromTop(4);

        auto row = area.removeFromTop(24);
        resetButton.setBounds(row.removeFromRight(60));
        row.removeFromRight(6);
        slider.setBounds(row);
    }

private:
    std::function<void(float)> onChange;
    juce::Label title;
    juce::Slider slider;
    juce::TextButton resetButton { "Reset" };
};
