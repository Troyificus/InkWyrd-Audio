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
                                  TrackGainStore& trackGainsToUse,
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
      playlistPanel(libraryToUse, playlistToUse, trackGainsToUse,
                     std::move(onActivatePlaylistToUse),
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
        // Pause, not stop: this one keeps your place.
        if (playlist.isPlaying() && !playlist.isFadingOut())
            playlist.pause();
        else
            playlist.resume();

        updatePlayButtonText();
    };

    addAndMakeVisible(stopButton);
    stopButton.onClick = [this]
    {
        playlist.hardStop();
        refreshToggleStates();
    };

    addAndMakeVisible(fadeOutButton);
    fadeOutButton.onClick = [this]
    {
        playlist.fadeOutAndStop(fadeOutSlider.getValue());
        refreshToggleStates();
    };

    updateCrossfadeToggleText();
    addAndMakeVisible(crossfadeCaption);
    addAndMakeVisible(crossfadeToggle);
    crossfadeToggle.onClick = [this]
    {
        playlist.setCrossfadeEnabled(!playlist.isCrossfadeEnabled());
        updateCrossfadeToggleText();
        crossfadeSlider.setEnabled(playlist.isCrossfadeEnabled());

        if (onPlaybackSettingsChanged)
            onPlaybackSettingsChanged();
    };

    crossfadeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    crossfadeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 22);
    crossfadeSlider.setRange(PlaylistEngine::kMinCrossfadeSeconds,
                              PlaylistEngine::kMaxCrossfadeSeconds, 0.5);
    crossfadeSlider.setTextValueSuffix(" s");
    crossfadeSlider.onValueChange = [this]
    {
        playlist.setCrossfadeSeconds(crossfadeSlider.getValue());

        if (onPlaybackSettingsChanged)
            onPlaybackSettingsChanged();
    };
    addAndMakeVisible(crossfadeSlider);

    addAndMakeVisible(fadeOutCaption);
    fadeOutSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fadeOutSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 22);
    fadeOutSlider.setRange(1.0, 20.0, 0.5);
    fadeOutSlider.setTextValueSuffix(" s");
    fadeOutSlider.onValueChange = [this] { if (onPlaybackSettingsChanged) onPlaybackSettingsChanged(); };
    addAndMakeVisible(fadeOutSlider);

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

    masterVolumeCaption.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(masterVolumeCaption);

    masterVolumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    masterVolumeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 22);
    masterVolumeSlider.setRange(0.0, 100.0, 1.0);
    masterVolumeSlider.setTextValueSuffix("%");
    masterVolumeSlider.setValue(100.0, juce::dontSendNotification);
    masterVolumeSlider.onValueChange = [this]
    {
        auto gain = (float) (masterVolumeSlider.getValue() / 100.0);
        masterEngine.setMasterGain(gain);

        if (onMasterVolumeChanged)
            onMasterVolumeChanged(gain);
    };
    addAndMakeVisible(masterVolumeSlider);

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

void PlayerComponent::setMasterVolume(float volume)
{
    // dontSendNotification: this is restoring a saved value, not the user
    // moving the fader, so it must not write straight back to settings.
    masterVolumeSlider.setValue(juce::jlimit(0.0, 100.0, volume * 100.0), juce::dontSendNotification);
    masterEngine.setMasterGain(volume);
}

void PlayerComponent::setMasterVolumeChangedCallback(std::function<void(float)> callback)
{
    onMasterVolumeChanged = std::move(callback);
}

void PlayerComponent::setDiscordConfigured(bool configured)
{
    discordConfigured = configured;
    updateMonitorHint();
}

void PlayerComponent::updatePlayButtonText()
{
    // "Pause" rather than "Stop" now that a real Stop sits next to it -
    // two buttons both saying Stop, doing different things, would be
    // worse than either.
    playButton.setButtonText(playlist.isPlaying() && !playlist.isFadingOut() ? "Pause" : "Play");

    stopButton.setEnabled(playlist.isPlaying());
    fadeOutButton.setEnabled(playlist.isPlaying() && !playlist.isFadingOut());
    fadeOutButton.setButtonText(playlist.isFadingOut() ? "Fading..." : "Fade out");
}

void PlayerComponent::updateCrossfadeToggleText()
{
    crossfadeToggle.setButtonText(playlist.isCrossfadeEnabled() ? "On" : "Off");
}

void PlayerComponent::setPlaybackSettings(bool crossfadeEnabled, double crossfadeSeconds,
                                           double fadeOutSecondsToUse)
{
    playlist.setCrossfadeEnabled(crossfadeEnabled);
    playlist.setCrossfadeSeconds(crossfadeSeconds);

    // dontSendNotification: restoring saved values, not the user changing
    // them, so this must not write straight back to settings.
    crossfadeSlider.setValue(playlist.getCrossfadeSeconds(), juce::dontSendNotification);
    crossfadeSlider.setEnabled(crossfadeEnabled);
    fadeOutSlider.setValue(fadeOutSecondsToUse, juce::dontSendNotification);
    updateCrossfadeToggleText();
}

void PlayerComponent::setPlaybackSettingsChangedCallback(std::function<void()> callback)
{
    onPlaybackSettingsChanged = std::move(callback);
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

    // Row one is the transport - the things pressed during a session.
    auto buttonRow = area.removeFromTop(32);
    playButton.setBounds(buttonRow.removeFromLeft(80));
    buttonRow.removeFromLeft(8);
    stopButton.setBounds(buttonRow.removeFromLeft(70));
    buttonRow.removeFromLeft(8);
    fadeOutButton.setBounds(buttonRow.removeFromLeft(95));
    buttonRow.removeFromLeft(8);
    skipButton.setBounds(buttonRow.removeFromLeft(80));
    buttonRow.removeFromLeft(8);
    shuffleButton.setBounds(buttonRow.removeFromLeft(110));

    voiceFxButton.setBounds(buttonRow.removeFromRight(110));
    buttonRow.removeFromRight(12);
    masterVolumeSlider.setBounds(buttonRow.removeFromRight(180));
    masterVolumeCaption.setBounds(buttonRow.removeFromRight(56));

    area.removeFromTop(8);

    // Row two is how the app behaves - set once and mostly left alone.
    auto settingsRow = area.removeFromTop(28);
    muteButton.setBounds(settingsRow.removeFromLeft(100));
    settingsRow.removeFromLeft(8);
    monitorButton.setBounds(settingsRow.removeFromLeft(120));
    settingsRow.removeFromLeft(20);
    crossfadeCaption.setBounds(settingsRow.removeFromLeft(70));
    crossfadeToggle.setBounds(settingsRow.removeFromLeft(50));
    settingsRow.removeFromLeft(6);
    crossfadeSlider.setBounds(settingsRow.removeFromLeft(170));
    settingsRow.removeFromLeft(20);
    fadeOutCaption.setBounds(settingsRow.removeFromLeft(96));
    fadeOutSlider.setBounds(settingsRow.removeFromLeft(160));
    area.removeFromTop(16);

    playlistPanel.setBounds(area.removeFromLeft(kLeftColumnWidth));
    area.removeFromLeft(kColumnGap);
    soundboardGrid.setBounds(area);
}
