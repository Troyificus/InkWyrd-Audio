#include "AppSettings.h"

namespace
{
    constexpr const char* kPlaylistFolderKey = "playlistFolder";
    constexpr const char* kSoundboardFolderKey = "soundboardFolder";
    constexpr const char* kBotTokenKey = "discordBotToken";
    constexpr const char* kGuildIdKey = "discordGuildId";
    constexpr const char* kChannelIdKey = "discordChannelId";
    constexpr const char* kActivePlaylistIdKey = "activePlaylistId";
    constexpr const char* kPlaylistLibraryMigratedKey = "playlistLibraryMigrated";
    constexpr const char* kSoundboardLayoutMigratedKey = "soundboardLayoutMigrated";
    constexpr const char* kMasterVolumeKey = "masterVolume";
    constexpr const char* kCrossfadeEnabledKey = "crossfadeEnabled";
    constexpr const char* kCrossfadeSecondsKey = "crossfadeSeconds";
    constexpr const char* kFadeOutSecondsKey = "fadeOutSeconds";
    constexpr const char* kLoopEnabledKey = "loopEnabled";
    constexpr const char* kLoopGapSecondsKey = "loopGapSeconds";
    constexpr const char* kWindowLayoutKey = "windowLayout";
    constexpr const char* kTrackLibraryMigratedKey = "trackLibraryMigrated";
    constexpr const char* kLibraryFolderViewKey = "libraryFolderView";
    constexpr const char* kSkinNameKey = "skinName";
    constexpr const char* kLocalMonitoringKey = "localMonitoring";
    constexpr const char* kMicMutedKey = "micMuted";
    constexpr const char* kExampleSkinsWrittenKey = "exampleSkinsWritten";
    constexpr const char* kNoiseSuppressionKey = "noiseSuppressionEnabled";
    constexpr const char* kDiscordAutoMuteKey = "discordAutoMuteEnabled";
    constexpr const char* kDiscordClientSecretKey = "discordClientSecret";
    constexpr const char* kDiscordRpcRefreshTokenKey = "discordRpcRefreshToken";
}

AppSettings::AppSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Inkwyrd Audio";
    options.filenameSuffix = "settings";
    options.folderName = "Inkwyrd Audio";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.osxLibrarySubFolder = "Application Support";
    properties.setStorageParameters(options);
}

juce::PropertiesFile* AppSettings::settings() const
{
    return properties.getUserSettings();
}

juce::File AppSettings::getPlaylistFolder() const
{
    return juce::File(settings()->getValue(kPlaylistFolderKey));
}

void AppSettings::setPlaylistFolder(const juce::File& folder)
{
    settings()->setValue(kPlaylistFolderKey, folder.getFullPathName());
}

juce::File AppSettings::getSoundboardFolder() const
{
    return juce::File(settings()->getValue(kSoundboardFolderKey));
}

void AppSettings::setSoundboardFolder(const juce::File& folder)
{
    settings()->setValue(kSoundboardFolderKey, folder.getFullPathName());
}

juce::String AppSettings::getBotToken() const
{
    return settings()->getValue(kBotTokenKey);
}

void AppSettings::setBotToken(const juce::String& token)
{
    settings()->setValue(kBotTokenKey, token);
}

juce::String AppSettings::getGuildId() const
{
    return settings()->getValue(kGuildIdKey);
}

void AppSettings::setGuildId(const juce::String& guildId)
{
    settings()->setValue(kGuildIdKey, guildId);
}

juce::String AppSettings::getChannelId() const
{
    return settings()->getValue(kChannelIdKey);
}

void AppSettings::setChannelId(const juce::String& channelId)
{
    settings()->setValue(kChannelIdKey, channelId);
}

juce::String AppSettings::getActivePlaylistId() const
{
    return settings()->getValue(kActivePlaylistIdKey);
}

void AppSettings::setActivePlaylistId(const juce::String& id)
{
    settings()->setValue(kActivePlaylistIdKey, id);
}

bool AppSettings::isPlaylistLibraryMigrated() const
{
    return settings()->getBoolValue(kPlaylistLibraryMigratedKey, false);
}

void AppSettings::setPlaylistLibraryMigrated(bool migrated)
{
    settings()->setValue(kPlaylistLibraryMigratedKey, migrated);
}

float AppSettings::getMasterVolume() const
{
    return (float) juce::jlimit(0.0, 1.0, settings()->getDoubleValue(kMasterVolumeKey, 1.0));
}

void AppSettings::setMasterVolume(float volume)
{
    settings()->setValue(kMasterVolumeKey, (double) juce::jlimit(0.0f, 1.0f, volume));
}

bool AppSettings::isCrossfadeEnabled() const
{
    return settings()->getBoolValue(kCrossfadeEnabledKey, true);
}

void AppSettings::setCrossfadeEnabled(bool enabled)
{
    settings()->setValue(kCrossfadeEnabledKey, enabled);
}

double AppSettings::getCrossfadeSeconds() const
{
    return settings()->getDoubleValue(kCrossfadeSecondsKey, 3.0);
}

void AppSettings::setCrossfadeSeconds(double seconds)
{
    settings()->setValue(kCrossfadeSecondsKey, seconds);
}

double AppSettings::getFadeOutSeconds() const
{
    return settings()->getDoubleValue(kFadeOutSecondsKey, 5.0);
}

void AppSettings::setFadeOutSeconds(double seconds)
{
    settings()->setValue(kFadeOutSecondsKey, seconds);
}

bool AppSettings::isLoopEnabled() const
{
    return settings()->getBoolValue(kLoopEnabledKey, false);
}

void AppSettings::setLoopEnabled(bool enabled)
{
    settings()->setValue(kLoopEnabledKey, enabled);
}

double AppSettings::getLoopGapSeconds() const
{
    return settings()->getDoubleValue(kLoopGapSecondsKey, 0.0);
}

void AppSettings::setLoopGapSeconds(double seconds)
{
    settings()->setValue(kLoopGapSecondsKey, seconds);
}

bool AppSettings::isSoundboardLayoutMigrated() const
{
    return settings()->getBoolValue(kSoundboardLayoutMigratedKey, false);
}

void AppSettings::setSoundboardLayoutMigrated(bool migrated)
{
    settings()->setValue(kSoundboardLayoutMigratedKey, migrated);
}

juce::String AppSettings::getWindowLayoutJson() const
{
    return settings()->getValue(kWindowLayoutKey);
}

void AppSettings::setWindowLayoutJson(const juce::String& json)
{
    settings()->setValue(kWindowLayoutKey, json);
}

bool AppSettings::isTrackLibraryMigrated() const
{
    return settings()->getBoolValue(kTrackLibraryMigratedKey, false);
}

void AppSettings::setTrackLibraryMigrated(bool migrated)
{
    settings()->setValue(kTrackLibraryMigratedKey, migrated);
}

bool AppSettings::isLocalMonitoringEnabled() const
{
    // Off by default, which is what it has always come up as: the host
    // is usually in the call and would otherwise hear everything twice.
    return settings()->getBoolValue(kLocalMonitoringKey, false);
}

void AppSettings::setLocalMonitoringEnabled(bool enabled)
{
    settings()->setValue(kLocalMonitoringKey, enabled);
}

bool AppSettings::isMicMuted() const
{
    return settings()->getBoolValue(kMicMutedKey, false);
}

void AppSettings::setMicMuted(bool muted)
{
    settings()->setValue(kMicMutedKey, muted);
}

juce::String AppSettings::getSkinName() const
{
    return settings()->getValue(kSkinNameKey, {});
}

void AppSettings::setSkinName(const juce::String& name)
{
    settings()->setValue(kSkinNameKey, name);
}

bool AppSettings::areExampleSkinsWritten() const
{
    return settings()->getBoolValue(kExampleSkinsWrittenKey, false);
}

void AppSettings::setExampleSkinsWritten(bool written)
{
    settings()->setValue(kExampleSkinsWrittenKey, written);
}

bool AppSettings::isLibraryFolderView() const
{
    return settings()->getBoolValue(kLibraryFolderViewKey, false);
}

void AppSettings::setLibraryFolderView(bool shouldShowFolders)
{
    settings()->setValue(kLibraryFolderViewKey, shouldShowFolders);
}

bool AppSettings::isNoiseSuppressionEnabled() const
{
    return settings()->getBoolValue(kNoiseSuppressionKey, false);
}

void AppSettings::setNoiseSuppressionEnabled(bool enabled)
{
    settings()->setValue(kNoiseSuppressionKey, enabled);
}

bool AppSettings::isDiscordAutoMuteEnabled() const
{
    return settings()->getBoolValue(kDiscordAutoMuteKey, false);
}

void AppSettings::setDiscordAutoMuteEnabled(bool enabled)
{
    settings()->setValue(kDiscordAutoMuteKey, enabled);
}

juce::String AppSettings::getDiscordClientSecret() const
{
    return settings()->getValue(kDiscordClientSecretKey);
}

void AppSettings::setDiscordClientSecret(const juce::String& secret)
{
    settings()->setValue(kDiscordClientSecretKey, secret);
}

juce::String AppSettings::getDiscordRpcRefreshToken() const
{
    return settings()->getValue(kDiscordRpcRefreshTokenKey);
}

void AppSettings::setDiscordRpcRefreshToken(const juce::String& token)
{
    settings()->setValue(kDiscordRpcRefreshTokenKey, token);
}

bool AppSettings::isPlaylistFolderSet() const
{
    auto folder = getPlaylistFolder();
    return folder.getFullPathName().isNotEmpty() && folder.isDirectory();
}

bool AppSettings::hasDiscordCredentials() const
{
    return getBotToken().isNotEmpty() && getGuildId().isNotEmpty() && getChannelId().isNotEmpty();
}

void AppSettings::save()
{
    settings()->saveIfNeeded();
}
