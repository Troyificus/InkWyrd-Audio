#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Persists the small handful of values that used to be environment
// variables (PLAYLIST_FOLDER, SOUNDBOARD_FOLDER, DISCORD_BOT_TOKEN,
// DISCORD_GUILD_ID, DISCORD_CHANNEL_ID) to a real per-user settings file
// instead - %APPDATA%\Inkwyrd Audio\Inkwyrd Audio.settings - via JUCE's
// own juce::ApplicationProperties/PropertiesFile. Set once through the
// GUI, never touched again unless the user reopens Settings.
//
// The bot token is stored in plain XML on disk. That's an accepted
// tradeoff for this beta, not an oversight - the same pragmatic-security
// posture as ControlServer's unauthenticated loopback socket. No
// encryption is added here.
class AppSettings
{
public:
    AppSettings();

    juce::File getPlaylistFolder() const;
    void setPlaylistFolder(const juce::File& folder);

    juce::File getSoundboardFolder() const;
    void setSoundboardFolder(const juce::File& folder);

    juce::String getBotToken() const;
    void setBotToken(const juce::String& token);

    juce::String getGuildId() const;
    void setGuildId(const juce::String& guildId);

    juce::String getChannelId() const;
    void setChannelId(const juce::String& channelId);

    bool isPlaylistFolderSet() const;
    bool hasDiscordCredentials() const;

    void save();

private:
    juce::PropertiesFile* settings() const;

    // mutable: ApplicationProperties::getUserSettings() is non-const
    // (lazily creates the PropertiesFile on first access), but every
    // AppSettings getter is logically const.
    mutable juce::ApplicationProperties properties;
};
