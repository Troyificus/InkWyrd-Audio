#include "SetupComponent.h"

#include "PlaylistLibrary.h"
#include "SkinLoader.h"

SetupComponent::SetupComponent(AppSettings& settingsToUse,
                                bool isFirstRun,
                                std::function<void(Result)> onSaveAndLaunchToUse,
                                std::function<void(juce::String, AuthoriseCallback)> onAuthoriseRpcToUse,
                                ApplySkinCallback onApplySkinToUse)
    : settings(settingsToUse),
      onSaveAndLaunch(std::move(onSaveAndLaunchToUse)),
      onAuthoriseRpc(std::move(onAuthoriseRpcToUse)),
      onApplySkin(std::move(onApplySkinToUse))
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

    addAndMakeVisible(playlistFilesCaption);
    openPlaylistFolderButton.onClick = [this]
    {
        // The library owns the real location; asking AppSettings for the
        // music folder would open the wrong thing entirely.
        PlaylistLibrary::getDefaultDirectory().startAsProcess();
    };
    addAndMakeVisible(openPlaylistFolderButton);

    addAndMakeVisible(skinSectionCaption);

    skinBox.setTextWhenNothingSelected("Inkwyrd (built-in)");
    skinBox.onChange = [this] { applySelectedSkin(); };
    addAndMakeVisible(skinBox);

    openSkinsFolderButton.onClick = [this]
    {
        // Created on the way out: sending someone to a folder that isn't
        // there is worse than useless.
        auto folder = inkwyrd::SkinLoader::getDefaultFolder();
        folder.createDirectory();
        folder.startAsProcess();
    };
    addAndMakeVisible(openSkinsFolderButton);

    reloadSkinsButton.onClick = [this]
    {
        // Skins get edited in a text editor with the app running, so
        // re-reading has to be a button rather than a restart.
        refreshSkinList(settings.getSkinName());
        applySelectedSkin();
    };
    addAndMakeVisible(reloadSkinsButton);

    exportSkinButton.onClick = [this] { exportCurrentSkin(); };
    addAndMakeVisible(exportSkinButton);

    skinStatusLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    skinStatusLabel.setColour(juce::Label::textColourId, inkwyrd::theme::textDim);
    addAndMakeVisible(skinStatusLabel);

    refreshSkinList(settings.getSkinName());

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
    setSize(640, 760); // the skin section added ~90px
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

void SetupComponent::refreshSkinList(const juce::String& nameToSelect)
{
    skinFolders = inkwyrd::SkinLoader::findSkinFolders(inkwyrd::SkinLoader::getDefaultFolder());

    skinBox.clear(juce::dontSendNotification);
    skinBox.addItem("Inkwyrd (built-in)", 1);

    for (int i = 0; i < skinFolders.size(); ++i)
        skinBox.addItem(skinFolders[i].getFileName(), i + 2);

    // Falls back to built-in if the saved skin's folder has been renamed
    // or deleted since - silently, since that is the user's own doing.
    int idToSelect = 1;
    for (int i = 0; i < skinFolders.size(); ++i)
        if (skinFolders[i].getFileName() == nameToSelect)
            idToSelect = i + 2;

    skinBox.setSelectedId(idToSelect, juce::dontSendNotification);
}

void SetupComponent::applySelectedSkin()
{
    if (onApplySkin == nullptr)
        return;

    auto selected = skinBox.getSelectedId();
    auto name = selected >= 2 && juce::isPositiveAndBelow(selected - 2, skinFolders.size())
                    ? skinFolders[selected - 2].getFileName()
                    : juce::String();

    auto message = onApplySkin(name);

    // Persisted here and now rather than on Save & Apply: a skin is a
    // preference like the Library's view, and someone who picks one and
    // closes Settings means it.
    settings.setSkinName(name);
    settings.save(); // AppSettings has no autosave

    skinStatusLabel.setColour(juce::Label::textColourId,
                               message.isEmpty() ? inkwyrd::theme::textDim : inkwyrd::theme::warning);
    skinStatusLabel.setText(message.isEmpty()
                                ? (name.isEmpty() ? juce::String("Using the built-in look.")
                                                  : "Using " + name + ".")
                                : message,
                             juce::dontSendNotification);
}

void SetupComponent::exportCurrentSkin()
{
    auto* window = new juce::AlertWindow("Export current skin",
                                          "A folder of this name is written to your skins folder. "
                                          "Edit its skin.json to make it your own.",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", "My Skin");
    window->addButton("Export", 1);
    window->addButton("Cancel", 0);

    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, safeThis = juce::Component::SafePointer<SetupComponent>(this), window](int result)
    {
        std::unique_ptr<juce::AlertWindow> owned(window);
        if (result != 1 || safeThis == nullptr)
            return;

        auto name = owned->getTextEditorContents("name").trim();
        if (name.isEmpty())
            return;

        // A skin name becomes a folder name, so anything Windows won't
        // accept in one has to go.
        auto folderName = juce::File::createLegalFileName(name);
        auto folder = inkwyrd::SkinLoader::getDefaultFolder().getChildFile(folderName);

        juce::String error;
        if (! inkwyrd::SkinLoader::writeToFolder(inkwyrd::theme::current(), name, folder, error))
        {
            skinStatusLabel.setColour(juce::Label::textColourId, inkwyrd::theme::warning);
            skinStatusLabel.setText("Couldn't export: " + error, juce::dontSendNotification);
            return;
        }

        refreshSkinList(folderName);
        applySelectedSkin();
        folder.startAsProcess();
    }));
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

    auto playlistFilesRow = area.removeFromTop(28);
    playlistFilesCaption.setBounds(playlistFilesRow.removeFromLeft(140));
    openPlaylistFolderButton.setBounds(playlistFilesRow.removeFromLeft(190));
    area.removeFromTop(16);

    skinSectionCaption.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);

    auto skinRow = area.removeFromTop(28);
    skinBox.setBounds(skinRow.removeFromLeft(170));
    skinRow.removeFromLeft(8);
    openSkinsFolderButton.setBounds(skinRow.removeFromLeft(146));
    skinRow.removeFromLeft(8);
    reloadSkinsButton.setBounds(skinRow.removeFromLeft(74));
    skinRow.removeFromLeft(8);
    exportSkinButton.setBounds(skinRow.removeFromLeft(146));

    area.removeFromTop(4);
    skinStatusLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(16);

    saveAndLaunchButton.setBounds(area.removeFromTop(36).removeFromRight(160));
}
