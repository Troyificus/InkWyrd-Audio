#include "PlayerComponent.h"

namespace
{
    constexpr int kRowHeight = 28;
    constexpr int kRowSpacing = 4;

    void layoutRows(juce::Component& panel, juce::OwnedArray<juce::TextButton>& buttons, int width)
    {
        width = juce::jmax(0, width);
        int y = 0;
        for (auto* button : buttons)
        {
            button->setBounds(0, y, width, kRowHeight);
            y += kRowHeight + kRowSpacing;
        }
        panel.setSize(width, juce::jmax(kRowHeight, y));
    }
}

PlayerComponent::PlayerComponent(PlaylistEngine& playlistToUse, SoundboardEngine& soundboardToUse,
                                  MasterEngine& masterEngineToUse, PluginScanner& scannerToUse,
                                  PluginChain& voiceChainToUse, juce::Array<juce::PluginDescription> availablePluginsToUse,
                                  juce::StringArray soundNamesToUse, std::function<void()> onSettingsClickedToUse)
    : playlist(playlistToUse), soundboard(soundboardToUse), masterEngine(masterEngineToUse),
      scanner(scannerToUse), voiceChain(voiceChainToUse), availablePlugins(std::move(availablePluginsToUse))
{
    nowPlayingLabel.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    addAndMakeVisible(nowPlayingLabel);

    discordStatusLabel.setText("Local monitor only - no Discord credentials configured.", juce::dontSendNotification);
    discordStatusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(discordStatusLabel);

    audioDeviceStatusLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    audioDeviceStatusLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    addAndMakeVisible(audioDeviceStatusLabel);

    addAndMakeVisible(skipButton);
    skipButton.onClick = [this] { playlist.skipToNext(); };

    updateShuffleButtonText();
    addAndMakeVisible(shuffleButton);
    shuffleButton.onClick = [this]
    {
        playlist.setShuffle(!playlist.isShuffleEnabled());
        updateShuffleButtonText();
    };

    updateMuteButtonText();
    addAndMakeVisible(muteButton);
    muteButton.onClick = [this]
    {
        masterEngine.setMicMuted(!masterEngine.isMicMuted());
        updateMuteButtonText();
    };

    addAndMakeVisible(settingsButton);
    settingsButton.onClick = [onSettingsClickedToUse] { if (onSettingsClickedToUse) onSettingsClickedToUse(); };

    addAndMakeVisible(soundboardCaption);
    soundboardViewport.setViewedComponent(&soundboardPanel, false);
    addAndMakeVisible(soundboardViewport);
    for (const auto& name : soundNamesToUse)
    {
        auto* button = soundboardButtons.add(new juce::TextButton(name));
        soundboardPanel.addAndMakeVisible(button);
        button->onClick = [this, name] { soundboard.trigger(name); };
    }

    addAndMakeVisible(pluginListCaption);
    pluginListViewport.setViewedComponent(&pluginListPanel, false);
    addAndMakeVisible(pluginListViewport);
    for (int i = 0; i < availablePlugins.size(); ++i)
    {
        auto description = availablePlugins.getReference(i);
        auto* button = addPluginButtons.add(new juce::TextButton("Add: " + description.name));
        pluginListPanel.addAndMakeVisible(button);
        button->onClick = [this, description]
        {
            juce::String error;
            if (!voiceChain.addPlugin(scanner, description, error))
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Couldn't add plugin", error);
            rebuildChainListUI();
        };
    }

    addAndMakeVisible(chainListCaption);
    chainListViewport.setViewedComponent(&chainListPanel, false);
    addAndMakeVisible(chainListViewport);
    rebuildChainListUI();

    startTimer(500);

    // setContentOwned(..., true) resizes the window to fit this
    // component's own size, so an explicit size here IS the window size.
    setSize(900, 620);
}

void PlayerComponent::setDiscordStatus(const juce::String& text)
{
    discordStatusLabel.setText(text, juce::dontSendNotification);
}

void PlayerComponent::setAudioDeviceStatus(const juce::String& text)
{
    audioDeviceStatusLabel.setText(text, juce::dontSendNotification);
    resized(); // the banner's presence changes how much height everything below it gets
}

void PlayerComponent::timerCallback()
{
    auto text = "Now playing: " + playlist.getCurrentTrackName();
    if (playlist.isCrossfading())
        text += " (crossfading)";
    nowPlayingLabel.setText(text, juce::dontSendNotification);

    // Shuffle/mute can also change via the Stream Deck plugin's
    // ControlServer commands, not just this component's own buttons -
    // refresh both labels here so an external change shows up too.
    updateShuffleButtonText();
    updateMuteButtonText();
}

void PlayerComponent::updateShuffleButtonText()
{
    shuffleButton.setButtonText(playlist.isShuffleEnabled() ? "Shuffle: On" : "Shuffle: Off");
}

void PlayerComponent::updateMuteButtonText()
{
    muteButton.setButtonText(masterEngine.isMicMuted() ? "Mic: Muted" : "Mic: Live");
}

void PlayerComponent::rebuildChainListUI()
{
    removeChainButtons.clear();

    auto n = voiceChain.getNumPlugins();
    for (int i = 0; i < n; ++i)
    {
        auto* button = removeChainButtons.add(new juce::TextButton("Remove: " + voiceChain.getPluginName(i)));
        chainListPanel.addAndMakeVisible(button);
        button->onClick = [this, i]
        {
            if (i < voiceChain.getNumPlugins())
                voiceChain.removePlugin(i);
            rebuildChainListUI();
        };
    }

    layoutRows(chainListPanel, removeChainButtons, chainListViewport.getWidth() - chainListViewport.getScrollBarThickness());
}

void PlayerComponent::resized()
{
    auto area = getLocalBounds().reduced(24);

    nowPlayingLabel.setBounds(area.removeFromTop(28));
    discordStatusLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(12);

    if (audioDeviceStatusLabel.getText().isNotEmpty())
    {
        audioDeviceStatusLabel.setBounds(area.removeFromTop(22));
        area.removeFromTop(12);
    }
    else
    {
        audioDeviceStatusLabel.setBounds(0, 0, 0, 0);
    }

    auto buttonRow = area.removeFromTop(32);
    skipButton.setBounds(buttonRow.removeFromLeft(90));
    buttonRow.removeFromLeft(8);
    shuffleButton.setBounds(buttonRow.removeFromLeft(110));
    buttonRow.removeFromLeft(8);
    muteButton.setBounds(buttonRow.removeFromLeft(100));
    settingsButton.setBounds(buttonRow.removeFromRight(90));
    area.removeFromTop(20);

    auto listsArea = area;
    auto columnWidth = (listsArea.getWidth() - 24) / 3;

    auto soundboardColumn = listsArea.removeFromLeft(columnWidth);
    listsArea.removeFromLeft(12);
    auto pluginColumn = listsArea.removeFromLeft(columnWidth);
    listsArea.removeFromLeft(12);
    auto chainColumn = listsArea;

    soundboardCaption.setBounds(soundboardColumn.removeFromTop(22));
    soundboardViewport.setBounds(soundboardColumn);
    layoutRows(soundboardPanel, soundboardButtons, soundboardViewport.getWidth() - soundboardViewport.getScrollBarThickness());

    pluginListCaption.setBounds(pluginColumn.removeFromTop(22));
    pluginListViewport.setBounds(pluginColumn);
    layoutRows(pluginListPanel, addPluginButtons, pluginListViewport.getWidth() - pluginListViewport.getScrollBarThickness());

    chainListCaption.setBounds(chainColumn.removeFromTop(22));
    chainListViewport.setBounds(chainColumn);
    layoutRows(chainListPanel, removeChainButtons, chainListViewport.getWidth() - chainListViewport.getScrollBarThickness());
}
