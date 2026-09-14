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

    // Master fader position, 0..1. Remembered between sessions - coming
    // back up at full volume after someone deliberately turned it down
    // would be a nasty surprise mid-session.
    float getMasterVolume() const;
    void setMasterVolume(float volume);

    // Crossfade between tracks, and how long the Fade out button takes.
    // Global rather than per-playlist: these are how the host likes the
    // app to behave, not a property of any particular list.
    bool isCrossfadeEnabled() const;
    void setCrossfadeEnabled(bool enabled);

    double getCrossfadeSeconds() const;
    void setCrossfadeSeconds(double seconds);

    double getFadeOutSeconds() const;
    void setFadeOutSeconds(double seconds);

    // Repeat the current track, and the silence between repeats.
    bool isLoopEnabled() const;
    void setLoopEnabled(bool enabled);

    double getLoopGapSeconds() const;
    void setLoopGapSeconds(double seconds);

    // Which playlist was last activated, so the app comes back up on it.
    juce::String getActivePlaylistId() const;
    void setActivePlaylistId(const juce::String& id);

    // Whether the one-time "wrap the old single music folder as a
    // playlist" migration has run. An explicit flag rather than
    // "is the library empty?", so deleting every playlist doesn't
    // resurrect the legacy one on the next launch.
    bool isPlaylistLibraryMigrated() const;
    void setPlaylistLibraryMigrated(bool migrated);

    // Same idea for the soundboard: whether the old "the board IS the
    // contents of the sound-effects folder" behaviour has been converted
    // into assignable slots. Explicit, so clearing every button doesn't
    // re-import the folder on the next launch.
    bool isSoundboardLayoutMigrated() const;
    void setSoundboardLayoutMigrated(bool migrated);

    // One JSON blob holding every DetachableWindow's bounds+visibility,
    // keyed by window name. A single string key rather than one per
    // window/field - PropertiesFile only stores flat scalars, and this
    // needs a variable-length, variable-window-count structure.
    juce::String getWindowLayoutJson() const;
    void setWindowLayoutJson(const juce::String& json);

    // Whether the one-time "union every existing playlist's tracks into
    // the new master Track Library" migration has run. Same explicit-flag
    // reasoning as the other two migration flags above.
    // Monitor and mic, as they were when the app last closed. Both are
    // switches the user sets deliberately for how they work, so coming
    // back up on someone else's defaults is a small annoyance every
    // session.
    bool isLocalMonitoringEnabled() const;
    void setLocalMonitoringEnabled(bool enabled);

    bool isMicMuted() const;
    void setMicMuted(bool muted);

    // The folder name of the skin in use, under
    // %APPDATA%\Inkwyrd Audio\skins. Empty means the built-in look.
    juce::String getSkinName() const;
    void setSkinName(const juce::String& name);

    // Whether the example skins have been written out. An explicit flag,
    // like the other one-time steps here, so deleting them doesn't bring
    // them back on the next launch.
    bool areExampleSkinsWritten() const;
    void setExampleSkinsWritten(bool written);

    // Which view the Library window's track pane was last showing:
    // false = the sortable table, true = the folder tree. A view is a
    // preference, so it survives a restart.
    bool isLibraryFolderView() const;
    void setLibraryFolderView(bool shouldShowFolders);

    bool isTrackLibraryMigrated() const;
    void setTrackLibraryMigrated(bool migrated);

    // RNNoise on the mic. Defaults to OFF, and that default is a
    // measured decision rather than caution: on an already-quiet mic
    // (20dB SNR) suppression costs about 9dB of speech-band SNR, so
    // switching it on for everyone would make most people sound worse.
    // See CLAUDE.md's RNNoise section for the numbers.
    bool isNoiseSuppressionEnabled() const;
    void setNoiseSuppressionEnabled(bool enabled);

    // Auto-muting the user's own Discord client while their mic is live
    // in Inkwyrd. Off by default and deliberately so: plenty of people
    // will never use the voice FX at all, and an app that mutes you in
    // Discord unasked is hostile.
    //
    // No client ID is stored - it's derived from the bot token, which is
    // issued by the same Discord application (see
    // DiscordRpcClient::deriveApplicationId).
    //
    // The client secret and refresh token sit in the same plaintext XML
    // as the bot token. Same accepted tradeoff, same beta-grade posture -
    // noted rather than hidden. Both are scoped to the user's own
    // application and grant no server access.
    bool isDiscordAutoMuteEnabled() const;
    void setDiscordAutoMuteEnabled(bool enabled);

    juce::String getDiscordClientSecret() const;
    void setDiscordClientSecret(const juce::String& secret);

    juce::String getDiscordRpcRefreshToken() const;
    void setDiscordRpcRefreshToken(const juce::String& token);

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
