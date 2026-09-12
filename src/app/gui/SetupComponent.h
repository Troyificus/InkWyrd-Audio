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
        juce::String discordClientSecret;
        bool discordAutoMuteEnabled = false;
    };

    // isFirstRun distinguishes the initial setup screen (where the
    // button really does launch the app) from reopening Settings later
    // (where it applies changes to the session already running).
    //
    // onAuthoriseRpc runs the one-time Discord consent flow with
    // whatever is currently typed in - it needs the live secret, not the
    // saved one, so someone can paste a secret and authorise without
    // saving and reopening Settings first. Its callback reports back
    // here for display.
    using AuthoriseCallback = std::function<void(bool success, juce::String message)>;

    // Applies a skin by folder name ({} for the built-in look) and
    // returns a message to show - empty when it loaded cleanly. Applying
    // happens the moment it is picked rather than on Save, because the
    // whole point of choosing a skin is seeing it.
    using ApplySkinCallback = std::function<juce::String(const juce::String& skinName)>;

    SetupComponent(AppSettings& settingsToUse,
                    bool isFirstRun,
                    std::function<void(Result)> onSaveAndLaunchToUse,
                    std::function<void(juce::String secret, AuthoriseCallback)> onAuthoriseRpcToUse,
                    ApplySkinCallback onApplySkinToUse = {});

    void resized() override;

private:
    void browseForFolder(juce::Label& targetLabel, juce::File& targetValue, const juce::String& chooserTitle);
    void updateSaveButtonEnablement();

    void refreshSkinList(const juce::String& nameToSelect);
    void applySelectedSkin();
    void exportCurrentSkin();

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

    // Auto-mute. Its own section rather than more fields in the Discord
    // block above, because it is a genuinely separate opt-in with its own
    // consent step - and because someone who only wants a music bot
    // should be able to see at a glance that they can ignore all of it.
    juce::Label autoMuteSectionCaption { {}, "Mute me in Discord while my mic is live (optional)" };
    juce::ToggleButton autoMuteToggle { "Enable" };
    juce::Label clientSecretCaption { {}, "Client secret" };
    juce::TextEditor clientSecretEditor;
    juce::TextButton authoriseButton { "Authorise..." };
    juce::Label autoMuteStatusLabel;

    // Moved here from under the playlist list in the Library window. It
    // opens the folder the playlist JSON files live in - useful once in
    // a while, and not something worth a permanent button next to the
    // ones used every session.
    juce::Label playlistFilesCaption { {}, "Playlist files" };
    juce::TextButton openPlaylistFolderButton { "Open playlists folder" };

    // Skins. A skin is a folder of its own under %APPDATA%\Inkwyrd Audio    // skins; "Export current..." writes what is on screen out as one,
    // which is the starting point for editing rather than typing a file
    // from scratch.
    juce::Label skinSectionCaption { {}, "Skin" };
    juce::ComboBox skinBox;
    juce::TextButton openSkinsFolderButton { "Open skins folder" };
    juce::TextButton reloadSkinsButton { "Reload" };
    juce::TextButton exportSkinButton { "Export current..." };
    juce::Label skinStatusLabel;

    // Parallel to the combo's items from id 2 up; id 1 is the built-in
    // look, which has no folder.
    juce::Array<juce::File> skinFolders;
    ApplySkinCallback onApplySkin;

    juce::TextButton saveAndLaunchButton;

    std::function<void(juce::String, AuthoriseCallback)> onAuthoriseRpc;

    void updateAutoMuteStatus(const juce::String& message, bool isError);

    std::unique_ptr<juce::FileChooser> activeChooser;
};
