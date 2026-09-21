#include "PlayerComponent.h"

#include <iterator>

#include "InkwyrdTheme.h"

namespace
{
    constexpr int kMargin = 16;
}

PlayerComponent::PlayerComponent(PlaylistEngine& playlistToUse,
                                  MasterEngine& masterEngineToUse,
                                  const TrackMetadataStore& trackMetadata,
                                  std::function<void()> onTogglePlaylistToUse,
                                  std::function<void()> onToggleLibraryToUse,
                                  std::function<void()> onToggleVoiceFxToUse,
                                  std::function<void()> onToggleSoundboardToUse,
                                  std::function<void()> onToggleScenesToUse,
                                  std::function<void()> onSettingsClickedToUse)
    : playlist(playlistToUse),
      masterEngine(masterEngineToUse),
      onTogglePlaylist(std::move(onTogglePlaylistToUse)),
      onToggleLibrary(std::move(onToggleLibraryToUse)),
      onToggleVoiceFx(std::move(onToggleVoiceFxToUse)),
      onToggleSoundboard(std::move(onToggleSoundboardToUse)),
      onToggleScenes(std::move(onToggleScenesToUse))
{
    nowPlaying = std::make_unique<NowPlayingDisplay>(playlist, masterEngine.getSpectrumTap(), trackMetadata);
    addAndMakeVisible(*nowPlaying);

    discordStatusLabel.setText("Local monitor only - no Discord credentials configured.", juce::dontSendNotification);
    addAndMakeVisible(discordStatusLabel);

    warningBannerLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    addAndMakeVisible(warningBannerLabel);

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

    updateLoopToggleText();
    addAndMakeVisible(loopCaption);
    addAndMakeVisible(loopToggle);
    loopToggle.onClick = [this]
    {
        playlist.setLoopEnabled(!playlist.isLoopEnabled());
        updateLoopToggleText();
        loopGapSlider.setEnabled(playlist.isLoopEnabled());

        if (onPlaybackSettingsChanged)
            onPlaybackSettingsChanged();
    };

    loopGapSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    loopGapSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 22);
    loopGapSlider.setRange(0.0, PlaylistEngine::kMaxLoopGapSeconds, 0.1);
    loopGapSlider.textFromValueFunction = [](double value)
    {
        // "0.0 s" would read as a setting rather than as "straight back
        // round with no break", which is what it actually means.
        return value <= 0.0 ? juce::String("No gap") : juce::String(value, 1) + " s";
    };
    loopGapSlider.onValueChange = [this]
    {
        playlist.setLoopGapSeconds(loopGapSlider.getValue());

        if (onPlaybackSettingsChanged)
            onPlaybackSettingsChanged();
    };
    addAndMakeVisible(loopGapSlider);

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

    addAndMakeVisible(playlistButton);
    playlistButton.onClick = [this] { if (onTogglePlaylist) onTogglePlaylist(); };

    addAndMakeVisible(libraryButton);
    libraryButton.onClick = [this] { if (onToggleLibrary) onToggleLibrary(); };

    addAndMakeVisible(voiceFxButton);
    voiceFxButton.onClick = [this] { if (onToggleVoiceFx) onToggleVoiceFx(); };

    addAndMakeVisible(soundboardButton);
    soundboardButton.onClick = [this] { if (onToggleSoundboard) onToggleSoundboard(); };

    addAndMakeVisible(scenesButton);
    scenesButton.onClick = [this] { if (onToggleScenes) onToggleScenes(); };

    addAndMakeVisible(settingsButton);
    settingsButton.onClick = [onSettingsClickedToUse] { if (onSettingsClickedToUse) onSettingsClickedToUse(); };

    // So the keyboard shortcuts work as soon as the window is focused,
    // without having to click something first.
    setWantsKeyboardFocus(true);

    startTimer(500);

    // The label colours this component owns. Also re-applied on a
    // skin change - see lookAndFeelChanged().
    lookAndFeelChanged();

    // setContentOwned(..., true) resizes the window to fit this
    // component's own size, so an explicit size here IS the window size.
    // Tall enough for every row PLUS the warning banner and monitor hint
    // both showing at once - the worst case, not just the common one.
    setSize(640, 596);
}

bool PlayerComponent::keyPressed(const juce::KeyPress& key)
{
    // Deliberately the same set every media player and video site uses,
    // rather than anything clever nobody would guess.
    if (key == juce::KeyPress::spaceKey)
    {
        playButton.triggerClick();
        return true;
    }

    if (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S')
    {
        stopButton.triggerClick();
        return true;
    }

    if (key.getTextCharacter() == 'm' || key.getTextCharacter() == 'M')
    {
        muteButton.triggerClick();
        return true;
    }

    // Escape is the panic key: silence every soundboard voice, and
    // deliberately NOT the music - "I fired the wrong effect" and "end
    // the session" are different emergencies, and Stop already covers
    // the second one.
    if (key == juce::KeyPress::escapeKey)
    {
        masterEngine.getSoundboard().stopAllVoices();
        return true;
    }

    if (key == juce::KeyPress::rightKey)
    {
        skipButton.triggerClick();
        return true;
    }

    // Volume in steps of 5, which is a usable nudge on a 0-100 fader.
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        auto step = key == juce::KeyPress::upKey ? 5.0 : -5.0;
        masterVolumeSlider.setValue(masterVolumeSlider.getValue() + step,
                                     juce::sendNotificationSync);
        return true;
    }

    return false;
}

void PlayerComponent::setDiscordStatus(const juce::String& text)
{
    discordStatusLabel.setText(text, juce::dontSendNotification);
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

void PlayerComponent::updateLoopToggleText()
{
    loopToggle.setButtonText(playlist.isLoopEnabled() ? "On" : "Off");
}

void PlayerComponent::setPlaybackSettings(bool crossfadeEnabled, double crossfadeSeconds,
                                           double fadeOutSecondsToUse,
                                           bool loopEnabled, double loopGapSeconds)
{
    playlist.setCrossfadeEnabled(crossfadeEnabled);
    playlist.setCrossfadeSeconds(crossfadeSeconds);
    playlist.setLoopEnabled(loopEnabled);
    playlist.setLoopGapSeconds(loopGapSeconds);

    // dontSendNotification: restoring saved values, not the user changing
    // them, so this must not write straight back to settings.
    crossfadeSlider.setValue(playlist.getCrossfadeSeconds(), juce::dontSendNotification);
    crossfadeSlider.setEnabled(crossfadeEnabled);
    fadeOutSlider.setValue(fadeOutSecondsToUse, juce::dontSendNotification);

    loopGapSlider.setValue(playlist.getLoopGapSeconds(), juce::dontSendNotification);
    loopGapSlider.setEnabled(loopEnabled);

    updateCrossfadeToggleText();
    updateLoopToggleText();
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

void PlayerComponent::timerCallback()
{
    // The now-playing readout repaints itself on its own timer - it has
    // to, for the spectrum - so there is nothing to push into it here.

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

    auto headerRow = area.removeFromTop(26);
    settingsButton.setBounds(headerRow.removeFromRight(90));
    headerRow.removeFromRight(8);
    discordStatusLabel.setBounds(headerRow);
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

    // The screen gets a fixed, generous share off the top. Fixed rather
    // than proportional because the rows below it have real minimum
    // heights - letting the display grow with the window would squeeze
    // the transport before it squeezed anything decorative.
    if (nowPlaying != nullptr)
    {
        nowPlaying->setBounds(area.removeFromTop(juce::jmin(224, juce::jmax(150, area.getHeight() - 150))));
        area.removeFromTop(10);
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

    area.removeFromTop(8);

    // Row two is how the app behaves - set once and mostly left alone.
    auto settingsRow = area.removeFromTop(28);
    muteButton.setBounds(settingsRow.removeFromLeft(100));
    settingsRow.removeFromLeft(8);
    monitorButton.setBounds(settingsRow.removeFromLeft(120));
    settingsRow.removeFromLeft(18);
    crossfadeCaption.setBounds(settingsRow.removeFromLeft(68));
    crossfadeToggle.setBounds(settingsRow.removeFromLeft(46));
    settingsRow.removeFromLeft(4);
    crossfadeSlider.setBounds(settingsRow.removeFromLeft(140));
    area.removeFromTop(8);

    auto loopRow = area.removeFromTop(28);
    loopCaption.setBounds(loopRow.removeFromLeft(66));
    loopToggle.setBounds(loopRow.removeFromLeft(46));
    loopRow.removeFromLeft(4);
    loopGapSlider.setBounds(loopRow.removeFromLeft(140));
    loopRow.removeFromLeft(18);
    fadeOutCaption.setBounds(loopRow.removeFromLeft(70));
    fadeOutSlider.setBounds(loopRow.removeFromLeft(140));
    area.removeFromTop(8);

    // Row three: master volume on the right.
    auto volumeRow = area.removeFromTop(28);
    masterVolumeSlider.setBounds(volumeRow.removeFromRight(180));
    volumeRow.removeFromRight(12);
    masterVolumeCaption.setBounds(volumeRow.removeFromRight(56));
    area.removeFromTop(8);

    // Row four: one activator per satellite window. Every window gets a
    // way back - closing one with its X used to strand it.
    //
    // Anchored to the BOTTOM rather than stacked after the row above it.
    // The window is sized for the worst case (warning banner AND monitor
    // hint both showing), so on the common run where neither does, that
    // spare height would otherwise pool as dead space under this row
    // instead of above it.
    auto activatorRow = area.removeFromBottom(28);
    juce::TextButton* activators[] = { &playlistButton, &libraryButton,
                                        &voiceFxButton, &soundboardButton, &scenesButton };

    constexpr int gap = 8;
    constexpr int count = (int) std::size(activators);
    auto buttonWidth = (activatorRow.getWidth() - gap * (count - 1)) / count;

    for (auto* button : activators)
    {
        button->setBounds(activatorRow.removeFromLeft(buttonWidth));
        activatorRow.removeFromLeft(gap);
    }
}

// Label colours are COPIES of the palette taken when the component is
// built, so a skin change has to re-apply them - JUCE calls this on
// every child when a window sends a look-and-feel change.
void PlayerComponent::lookAndFeelChanged()
{
    discordStatusLabel.setColour(juce::Label::textColourId, inkwyrd::theme::textDim);
    warningBannerLabel.setColour(juce::Label::textColourId, inkwyrd::theme::warning);
    monitorHintLabel.setColour(juce::Label::textColourId, inkwyrd::theme::warning);
}
