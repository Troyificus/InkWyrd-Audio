#include "PlayerComponent.h"
#include "VoiceFxComponent.h"

namespace
{
    constexpr int kMargin = 16;
    constexpr int kLeftColumnWidth = 440;
    constexpr int kColumnGap = 16;
}

PlayerComponent::PlayerComponent(PlaylistEngine& playlistToUse,
                                  SoundboardEngine& soundboardToUse,
                                  MasterEngine& masterEngineToUse,
                                  PluginScanner& scannerToUse,
                                  PluginChain& voiceChainToUse,
                                  juce::Array<juce::PluginDescription> availablePluginsToUse,
                                  PlaylistLibrary& libraryToUse,
                                  SoundboardLayout& soundboardLayoutToUse,
                                  std::function<void(const juce::Uuid&)> onActivatePlaylistToUse,
                                  std::function<void()> onSoundboardLayoutChangedToUse,
                                  std::function<void(const juce::Uuid&)> onPlaylistEditedToUse,
                                  std::function<void()> onSettingsClickedToUse)
    : playlist(playlistToUse),
      soundboard(soundboardToUse),
      masterEngine(masterEngineToUse),
      scanner(scannerToUse),
      voiceChain(voiceChainToUse),
      availablePlugins(std::move(availablePluginsToUse)),
      playlistPanel(libraryToUse, playlistToUse, std::move(onActivatePlaylistToUse),
                     std::move(onPlaylistEditedToUse)),
      soundboardGrid(soundboardToUse, soundboardLayoutToUse, std::move(onSoundboardLayoutChangedToUse))
{
    nowPlayingLabel.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    addAndMakeVisible(nowPlayingLabel);

    discordStatusLabel.setText("Local monitor only - no Discord credentials configured.", juce::dontSendNotification);
    discordStatusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(discordStatusLabel);

    warningBannerLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    warningBannerLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    addAndMakeVisible(warningBannerLabel);

    monitorHintLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    monitorHintLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    addAndMakeVisible(monitorHintLabel);

    updatePlayButtonText();
    addAndMakeVisible(playButton);
    playButton.onClick = [this]
    {
        if (playlist.isPlaying())
            playlist.pause();
        else
            playlist.resume();

        updatePlayButtonText();
    };

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

    updateMonitorButtonText();
    addAndMakeVisible(monitorButton);
    monitorButton.onClick = [this]
    {
        masterEngine.setLocalMonitoring(!masterEngine.isLocalMonitoring());
        updateMonitorButtonText();
    };

    addAndMakeVisible(voiceFxButton);
    voiceFxButton.onClick = [this] { showVoiceFxWindow(); };

    addAndMakeVisible(settingsButton);
    settingsButton.onClick = [onSettingsClickedToUse] { if (onSettingsClickedToUse) onSettingsClickedToUse(); };

    addAndMakeVisible(playlistPanel);
    addAndMakeVisible(soundboardGrid);

    startTimer(500);

    // setContentOwned(..., true) resizes the window to fit this
    // component's own size, so an explicit size here IS the window size.
    setSize(1200, 760);
}

PlayerComponent::~PlayerComponent()
{
    if (voiceFxWindow != nullptr)
        delete voiceFxWindow.getComponent();
}

void PlayerComponent::setDiscordStatus(const juce::String& text)
{
    discordStatusLabel.setText(text, juce::dontSendNotification);
}

void PlayerComponent::setAvailablePlugins(juce::Array<juce::PluginDescription> plugins)
{
    availablePlugins = std::move(plugins);

    // If the panel happens to be open when a scan finishes, close it -
    // it was built from the old (probably empty) list and has no way to
    // grow new rows. Reopening shows the full list.
    if (voiceFxWindow != nullptr)
    {
        delete voiceFxWindow.getComponent();
        voiceFxWindow = nullptr;
    }
}

void PlayerComponent::setPluginScanInProgress(bool scanning)
{
    pluginScanInProgress = scanning;

    // Disabled rather than merely relabelled: the scan is writing to the
    // same PluginScanner the panel would be instantiating plugins from.
    voiceFxButton.setButtonText(scanning ? "Scanning..." : "Voice FX...");
    voiceFxButton.setEnabled(!scanning);
}

void PlayerComponent::setRescanPluginsCallback(std::function<void()> callback)
{
    onRescanPlugins = std::move(callback);
}

void PlayerComponent::setDiscordConfigured(bool configured)
{
    discordConfigured = configured;
    updateMonitorHint();
}

void PlayerComponent::updatePlayButtonText()
{
    playButton.setButtonText(playlist.isPlaying() ? "Stop" : "Play");
}

void PlayerComponent::updateMonitorHint()
{
    // Only worth saying when it actually means total silence. With
    // Discord configured, Monitor off is the normal, correct state - the
    // host hears the mix through the call - so nagging about it there
    // would just be noise.
    auto silent = !masterEngine.isLocalMonitoring() && !discordConfigured;
    auto text = silent ? juce::String("Monitor is off, so nothing is audible - turn it on to hear the mix "
                                        "through this computer's speakers.")
                        : juce::String();

    if (monitorHintLabel.getText() == text)
        return;

    monitorHintLabel.setText(text, juce::dontSendNotification);
    resized(); // the hint's presence changes how much height everything below it gets
}

void PlayerComponent::setWarningBanner(const juce::String& text)
{
    warningBannerLabel.setText(text, juce::dontSendNotification);
    resized(); // the banner's presence changes how much height everything below it gets
}

void PlayerComponent::refreshSoundboard()
{
    soundboardGrid.refresh();
}

void PlayerComponent::setPlayingPlaylistId(const juce::Uuid& id)
{
    playlistPanel.setPlayingPlaylistId(id);
}

void PlayerComponent::showVoiceFxWindow()
{
    if (voiceFxWindow != nullptr)
    {
        // Already open - focus it rather than stacking a second copy.
        voiceFxWindow->toFront(true);
        return;
    }

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Voice FX";
    options.content.setOwned(new VoiceFxComponent(scanner, voiceChain, availablePlugins,
                                                   [this] { if (onRescanPlugins) onRescanPlugins(); }));
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    voiceFxWindow = options.launchAsync();
}

void PlayerComponent::timerCallback()
{
    auto text = "Now playing: " + playlist.getCurrentTrackName();
    if (playlist.isCrossfading())
        text += " (crossfading)";
    nowPlayingLabel.setText(text, juce::dontSendNotification);

    // Only repaint the track list when the track actually changed - this
    // ticks twice a second and the list can be long.
    auto current = playlist.getCurrentTrackFile();
    if (current != lastSeenTrack)
    {
        lastSeenTrack = current;
        playlistPanel.repaint();
    }

    // Shuffle/mute can also change via the Stream Deck plugin's
    // ControlServer commands, and monitoring flips off when Discord
    // connects - refresh the labels here so external changes show up.
    refreshToggleStates();
}

void PlayerComponent::updateShuffleButtonText()
{
    shuffleButton.setButtonText(playlist.isShuffleEnabled() ? "Shuffle: On" : "Shuffle: Off");
}

void PlayerComponent::updateMuteButtonText()
{
    muteButton.setButtonText(masterEngine.isMicMuted() ? "Mic: Muted" : "Mic: Live");
}

void PlayerComponent::updateMonitorButtonText()
{
    monitorButton.setButtonText(masterEngine.isLocalMonitoring() ? "Monitor: On" : "Monitor: Off");
}

void PlayerComponent::refreshToggleStates()
{
    updateShuffleButtonText();
    updateMuteButtonText();
    updateMonitorButtonText();
    updatePlayButtonText();
    updateMonitorHint();
}

void PlayerComponent::resized()
{
    auto area = getLocalBounds().reduced(kMargin);

    auto headerRow = area.removeFromTop(28);
    settingsButton.setBounds(headerRow.removeFromRight(90));
    nowPlayingLabel.setBounds(headerRow);

    discordStatusLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(8);

    if (warningBannerLabel.getText().isNotEmpty())
    {
        warningBannerLabel.setBounds(area.removeFromTop(22));
        area.removeFromTop(8);
    }
    else
    {
        warningBannerLabel.setBounds(0, 0, 0, 0);
    }

    if (monitorHintLabel.getText().isNotEmpty())
    {
        monitorHintLabel.setBounds(area.removeFromTop(20));
        area.removeFromTop(6);
    }
    else
    {
        monitorHintLabel.setBounds(0, 0, 0, 0);
    }

    auto buttonRow = area.removeFromTop(32);
    playButton.setBounds(buttonRow.removeFromLeft(80));
    buttonRow.removeFromLeft(8);
    skipButton.setBounds(buttonRow.removeFromLeft(90));
    buttonRow.removeFromLeft(8);
    shuffleButton.setBounds(buttonRow.removeFromLeft(110));
    buttonRow.removeFromLeft(8);
    muteButton.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(8);
    monitorButton.setBounds(buttonRow.removeFromLeft(120));
    voiceFxButton.setBounds(buttonRow.removeFromRight(110));
    area.removeFromTop(16);

    playlistPanel.setBounds(area.removeFromLeft(kLeftColumnWidth));
    area.removeFromLeft(kColumnGap);
    soundboardGrid.setBounds(area);
}
