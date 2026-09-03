#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AppSettings.h"

// First-run / Settings screen: two folder Browse buttons and three
// Discord credential fields, all prefilled from AppSettings so reopening
// this view later (via the Player view's Settings button) shows what's
// currently configured rather than blanks. "Save & Launch" only enables
// once a playlist folder is chosen and the three Discord fields are
// either all filled in or all left empty (mirrors the console app's old
// all-or-nothing haveDiscordCredentials gate).
class SetupComponent : public juce::Component,
                        private juce::TextEditor::Listener
{
public:
    struct Result
    {
        juce::File playlistFolder;
        juce::File soundboardFolder;
        juce::String botToken, guildId, channelId;
    };

    SetupComponent(AppSettings& settingsToUse, std::function<void(Result)> onSaveAndLaunchToUse);

    void resized() override;

private:
    void browseForFolder(juce::Label& targetLabel, juce::File& targetValue, const juce::String& chooserTitle);
    void updateSaveButtonEnablement();

    // juce::TextEditor::Listener
    void textEditorTextChanged(juce::TextEditor&) override { updateSaveButtonEnablement(); }

    AppSettings& settings;
    std::function<void(Result)> onSaveAndLaunch;

    juce::Label titleLabel;

    juce::Label playlistFolderCaption { {}, "Music folder (required)" };
    juce::Label playlistFolderValueLabel { {}, "No folder chosen" };
    juce::TextButton browsePlaylistButton { "Browse..." };
    juce::File chosenPlaylistFolder;

    // "Import" rather than "Soundboard folder": the board itself is the
    // source of truth now, and saving this only ADDS anything new from
    // the folder onto free buttons - it never rebuilds a board the user
    // has arranged by hand.
    juce::Label soundboardFolderCaption { {}, "Sound effects folder to import (optional)" };
    juce::Label soundboardFolderValueLabel { {}, "No folder chosen" };
    juce::TextButton browseSoundboardButton { "Browse..." };
    juce::File chosenSoundboardFolder;

    juce::Label discordSectionCaption { {}, "Discord bot (optional - leave blank to run local-only)" };
    juce::Label botTokenCaption { {}, "Bot token" };
    juce::TextEditor botTokenEditor;
    juce::Label guildIdCaption { {}, "Server (guild) ID" };
    juce::TextEditor guildIdEditor;
    juce::Label channelIdCaption { {}, "Voice channel ID" };
    juce::TextEditor channelIdEditor;

    juce::TextButton saveAndLaunchButton { "Save & Launch" };

    std::unique_ptr<juce::FileChooser> activeChooser;
};
