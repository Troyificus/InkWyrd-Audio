#include "AppSettings.h"

namespace
{
    constexpr const char* kPlaylistFolderKey = "playlistFolder";
    constexpr const char* kSoundboardFolderKey = "soundboardFolder";
    constexpr const char* kBotTokenKey = "discordBotToken";
    constexpr const char* kGuildIdKey = "discordGuildId";
    constexpr const char* kChannelIdKey = "discordChannelId";
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
