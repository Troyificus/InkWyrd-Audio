#include "TrackGainStore.h"

namespace
{
    constexpr const char* kKeySchemaVersion = "schemaVersion";
    constexpr const char* kKeyGains = "gains";
}

juce::File TrackGainStore::getDefaultFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Inkwyrd Audio")
        .getChildFile("track-gains.json");
}

void TrackGainStore::setFile(const juce::File& file)
{
    storeFile = file;
}

juce::String TrackGainStore::keyFor(const juce::File& file)
{
    // Lowercased: Windows paths are case-insensitive, and the same track
    // reached through two differently-cased paths must not end up with
    // two different trims.
    return file.getFullPathName().toLowerCase();
}

void TrackGainStore::load()
{
    gainsByPath.clear();
    loadWarnings.clear();

    if (!storeFile.existsAsFile())
        return;

    auto parsed = juce::JSON::parse(storeFile.loadFileAsString());
    if (!parsed.isObject())
    {
        loadWarnings.add(storeFile.getFileName() + " could not be read (not valid JSON) - "
                          "track volumes have been reset to normal");
        return;
    }

    if ((int) parsed.getProperty(kKeySchemaVersion, 0) > kCurrentSchemaVersion)
    {
        // Left strictly alone, same rule as the playlist and soundboard
        // files: rewriting a newer version's file with older code would
        // quietly destroy whatever it added.
        loadWarnings.add(storeFile.getFileName() + " was made by a newer version of Inkwyrd Audio "
                          "and was not loaded");
        return;
    }

    if (auto* gains = parsed.getProperty(kKeyGains, juce::var()).getDynamicObject())
    {
        for (const auto& property : gains->getProperties())
        {
            auto db = (float) (double) property.value;
            if (db != 0.0f)
                gainsByPath[property.name.toString().toLowerCase()] =
                    juce::jlimit(kMinDb, kMaxDb, db);
        }
    }
}

bool TrackGainStore::hasGain(const juce::File& file) const
{
    return gainsByPath.find(keyFor(file)) != gainsByPath.end();
}

float TrackGainStore::getGainDb(const juce::File& file) const
{
    auto it = gainsByPath.find(keyFor(file));
    return it == gainsByPath.end() ? 0.0f : it->second;
}

float TrackGainStore::getLinearGain(const juce::File& file) const
{
    auto db = getGainDb(file);
    return db == 0.0f ? 1.0f : juce::Decibels::decibelsToGain(db);
}

void TrackGainStore::setGainDb(const juce::File& file, float db)
{
    db = juce::jlimit(kMinDb, kMaxDb, db);
    auto key = keyFor(file);

    if (juce::approximatelyEqual(db, 0.0f))
        gainsByPath.erase(key);   // back to normal - don't store a no-op
    else
        gainsByPath[key] = db;

    save();
}

void TrackGainStore::save()
{
    storeFile.getParentDirectory().createDirectory();

    juce::DynamicObject::Ptr gains = new juce::DynamicObject();
    for (const auto& entry : gainsByPath)
        gains->setProperty(entry.first, (double) entry.second);

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty(kKeySchemaVersion, kCurrentSchemaVersion);
    root->setProperty(kKeyGains, juce::var(gains.get()));

    // Atomic, same reasoning as the playlist and soundboard files.
    juce::TemporaryFile temp(storeFile);
    if (temp.getFile().replaceWithText(juce::JSON::toString(juce::var(root.get()), false)))
        temp.overwriteTargetFileWithTemporary();
}
