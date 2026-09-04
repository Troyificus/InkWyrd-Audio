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
