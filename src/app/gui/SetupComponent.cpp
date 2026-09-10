#include "SetupComponent.h"

SetupComponent::SetupComponent(AppSettings& settingsToUse,
                                bool isFirstRun,
                                std::function<void(Result)> onSaveAndLaunchToUse,
                                std::function<void(juce::String, AuthoriseCallback)> onAuthoriseRpcToUse)
    : settings(settingsToUse),
      onSaveAndLaunch(std::move(onSaveAndLaunchToUse)),
      onAuthoriseRpc(std::move(onAuthoriseRpcToUse))
{
    saveAndLaunchButton.setButtonText(isFirstRun ? "Save & Launch" : "Save & Apply");
    titleLabel.setText("Inkwyrd Audio - Setup", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel);

    chosenPlaylistFolder = settings.getPlaylistFolder();
    if (chosenPlaylistFolder.isDirectory())
        playlistFolderValueLabel.setText(chosenPlaylistFolder.getFullPathName(), juce::dontSendNotification);

    chosenSoundboardFolder = settings.getSoundboardFolder();
    if (chosenSoundboardFolder.isDirectory())
        soundboardFolderValueLabel.setText(chosenSoundboardFolder.getFullPathName(), juce::dontSendNotification);

    addAndMakeVisible(playlistFolderCaption);
    addAndMakeVisible(playlistFolderValueLabel);
    addAndMakeVisible(browsePlaylistButton);
    browsePlaylistButton.onClick = [this]
    {
        browseForFolder(playlistFolderValueLabel, chosenPlaylistFolder, "Choose your music folder");
    };

    addAndMakeVisible(soundboardFolderCaption);
    addAndMakeVisible(soundboardFolderValueLabel);
    addAndMakeVisible(browseSoundboardButton);
    browseSoundboardButton.onClick = [this]
    {
        browseForFolder(soundboardFolderValueLabel, chosenSoundboardFolder,
                         "Choose a folder of sound effects to import");
    };

    addAndMakeVisible(discordSectionCaption);

    addAndMakeVisible(botTokenCaption);
    botTokenEditor.setText(settings.getBotToken(), juce::dontSendNotification);
    botTokenEditor.setPasswordCharacter('*');
    botTokenEditor.addListener(this);
    addAndMakeVisible(botTokenEditor);

    addAndMakeVisible(guildIdCaption);
    guildIdEditor.setText(settings.getGuildId(), juce::dontSendNotification);
    guildIdEditor.addListener(this);
    addAndMakeVisible(guildIdEditor);

    addAndMakeVisible(channelIdCaption);
    channelIdEditor.setText(settings.getChannelId(), juce::dontSendNotification);
    channelIdEditor.addListener(this);
    addAndMakeVisible(channelIdEditor);

    addAndMakeVisible(autoMuteSectionCaption);

    autoMuteToggle.setToggleState(settings.isDiscordAutoMuteEnabled(), juce::dontSendNotification);
    addAndMakeVisible(autoMuteToggle);

    addAndMakeVisible(clientSecretCaption);
    clientSecretEditor.setText(settings.getDiscordClientSecret(), juce::dontSendNotification);
    clientSecretEditor.setPasswordCharacter('*');
    clientSecretEditor.addListener(this);
    addAndMakeVisible(clientSecretEditor);

    authoriseButton.onClick = [this]
    {
        if (! onAuthoriseRpc)
            return;

        authoriseButton.setEnabled(false);
        updateAutoMuteStatus("Waiting for you to click Authorise in Discord...", false);

        onAuthoriseRpc(clientSecretEditor.getText().trim(),
                        [safeThis = juce::Component::SafePointer<SetupComponent>(this)]
                        (bool success, juce::String message)
                        {
                            // The consent flow can outlive this screen -
                            // it waits on a human - so nothing here may
                            // assume the component still exists.
                            if (auto* self = safeThis.getComponent())
                            {
                                self->authoriseButton.setEnabled(true);
                                self->updateAutoMuteStatus(message, ! success);
                            }
                        });
    };
    addAndMakeVisible(authoriseButton);

    autoMuteStatusLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(autoMuteStatusLabel);

    if (settings.getDiscordRpcRefreshToken().isNotEmpty())
        updateAutoMuteStatus("Authorised. Inkwyrd can mute you in Discord.", false);
    else
        updateAutoMuteStatus("Not authorised yet. Paste the client secret, then click Authorise.", false);

    addAndMakeVisible(saveAndLaunchButton);
    saveAndLaunchButton.onClick = [this]
    {
        Result result;
        result.playlistFolder = chosenPlaylistFolder;
        result.soundboardFolder = chosenSoundboardFolder;
        result.botToken = botTokenEditor.getText().trim();
        result.guildId = guildIdEditor.getText().trim();
        result.channelId = channelIdEditor.getText().trim();
        result.discordClientSecret = clientSecretEditor.getText().trim();
        result.discordAutoMuteEnabled = autoMuteToggle.getToggleState();

        if (onSaveAndLaunch)
            onSaveAndLaunch(result);
    };

    updateSaveButtonEnablement();

    // setContentOwned(..., true) resizes the window to fit this
    // component's own size, so an explicit size here IS the window size.
    // Taller than it was: the auto-mute section adds three rows, and
    // leaving the height alone would have pushed Save off the bottom.
    setSize(640, 620);
}

void SetupComponent::browseForFolder(juce::Label& targetLabel, juce::File& targetValue, const juce::String& chooserTitle)
{
    activeChooser = std::make_unique<juce::FileChooser>(chooserTitle, targetValue.isDirectory() ? targetValue : juce::File());

    activeChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                [this, &targetLabel, &targetValue](const juce::FileChooser& chooser)
    {
        auto result = chooser.getResult();
        if (result != juce::File())
        {
            targetValue = result;
            targetLabel.setText(result.getFullPathName(), juce::dontSendNotification);
            updateSaveButtonEnablement();
        }
    });
}

void SetupComponent::updateAutoMuteStatus(const juce::String& message, bool isError)
{
    autoMuteStatusLabel.setText(message, juce::dontSendNotification);
    autoMuteStatusLabel.setColour(juce::Label::textColourId,
                                   isError ? juce::Colours::orangered : juce::Colours::grey);
}

void SetupComponent::updateSaveButtonEnablement()
{
    auto hasToken = botTokenEditor.getText().trim().isNotEmpty();
    auto hasGuild = guildIdEditor.getText().trim().isNotEmpty();
    auto hasChannel = channelIdEditor.getText().trim().isNotEmpty();

    // All three Discord fields must be filled in together, or all left
    // empty - matches the console app's old all-or-nothing gate.
    auto discordFieldsValid = (hasToken == hasGuild) && (hasGuild == hasChannel);

    saveAndLaunchButton.setEnabled(chosenPlaylistFolder.isDirectory() && discordFieldsValid);
}

void SetupComponent::resized()
{
    auto area = getLocalBounds().reduced(24);

    titleLabel.setBounds(area.removeFromTop(36));
    area.removeFromTop(16);

    auto playlistRow = area.removeFromTop(24);
    playlistFolderCaption.setBounds(playlistRow);
    area.removeFromTop(4);
    auto playlistValueRow = area.removeFromTop(28);
    browsePlaylistButton.setBounds(playlistValueRow.removeFromRight(100));
    playlistValueRow.removeFromRight(8);
    playlistFolderValueLabel.setBounds(playlistValueRow);
    area.removeFromTop(20);

    auto soundboardRow = area.removeFromTop(24);
    soundboardFolderCaption.setBounds(soundboardRow);
    area.removeFromTop(4);
    auto soundboardValueRow = area.removeFromTop(28);
    browseSoundboardButton.setBounds(soundboardValueRow.removeFromRight(100));
    soundboardValueRow.removeFromRight(8);
    soundboardFolderValueLabel.setBounds(soundboardValueRow);
    area.removeFromTop(28);

    discordSectionCaption.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);

    auto tokenRow = area.removeFromTop(28);
    botTokenCaption.setBounds(tokenRow.removeFromLeft(140));
    botTokenEditor.setBounds(tokenRow);
    area.removeFromTop(10);

    auto guildRow = area.removeFromTop(28);
    guildIdCaption.setBounds(guildRow.removeFromLeft(140));
    guildIdEditor.setBounds(guildRow);
    area.removeFromTop(10);

    auto channelRow = area.removeFromTop(28);
    channelIdCaption.setBounds(channelRow.removeFromLeft(140));
    channelIdEditor.setBounds(channelRow);
    area.removeFromTop(24);

    auto autoMuteCaptionRow = area.removeFromTop(24);
    autoMuteToggle.setBounds(autoMuteCaptionRow.removeFromRight(90));
    autoMuteSectionCaption.setBounds(autoMuteCaptionRow);
    area.removeFromTop(6);

    auto secretRow = area.removeFromTop(28);
    clientSecretCaption.setBounds(secretRow.removeFromLeft(140));
    authoriseButton.setBounds(secretRow.removeFromRight(110));
    secretRow.removeFromRight(8);
    clientSecretEditor.setBounds(secretRow);
    area.removeFromTop(4);

    autoMuteStatusLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(20);

    saveAndLaunchButton.setBounds(area.removeFromTop(36).removeFromRight(160));
}
