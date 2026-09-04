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

    // Optional second control, used by the playlist's tracks: how long
    // this one takes to fade into whatever follows it.
    //
    // 0 means "use whatever the global crossfade length is", which is
    // what every track does until told otherwise - so the slider says
    // "Default" there rather than "0.0 s", which would read as "cut
    // straight over".
    void addFadeControl(double initialSeconds,
                         double maxSeconds,
                         std::function<void(double)> onFadeChanged)
    {
        onFadeChange = std::move(onFadeChanged);

        fadeCaption.setText("Fade into next", juce::dontSendNotification);
        fadeCaption.setFont(juce::Font(juce::FontOptions(12.0f)));
        fadeCaption.setColour(juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible(fadeCaption);

        fadeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        fadeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 20);
        fadeSlider.setRange(0.0, maxSeconds, 0.5);
        fadeSlider.setValue(initialSeconds, juce::dontSendNotification);
        fadeSlider.textFromValueFunction = [](double value)
        {
            return value <= 0.0 ? juce::String("Default") : juce::String(value, 1) + " s";
        };
        fadeSlider.updateText();
        fadeSlider.onValueChange = [this]
        {
            if (onFadeChange)
                onFadeChange(fadeSlider.getValue());
        };
        addAndMakeVisible(fadeSlider);

        hasFadeControl = true;
        setSize(getWidth(), 122);
        resized();
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

        if (!hasFadeControl)
            return;

        area.removeFromTop(6);
        fadeCaption.setBounds(area.removeFromTop(16));
        fadeSlider.setBounds(area.removeFromTop(24));
    }

private:
    std::function<void(float)> onChange;
    std::function<void(double)> onFadeChange;
    juce::Label title;
    juce::Slider slider;
    juce::TextButton resetButton { "Reset" };

    bool hasFadeControl = false;
    juce::Label fadeCaption;
    juce::Slider fadeSlider;
};
