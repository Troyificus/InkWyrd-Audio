#include "TrackSettingsStore.h"

namespace
{
    constexpr const char* kKeySchemaVersion = "schemaVersion";
    constexpr const char* kKeyTracks = "tracks";
    constexpr const char* kKeyGainDb = "gainDb";
    constexpr const char* kKeyFadeSeconds = "fadeSeconds";

    // The beta.7 file: a flat { path: dB } map under "gains".
    constexpr const char* kLegacyKeyGains = "gains";
}

juce::File TrackSettingsStore::getDefaultFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Inkwyrd Audio")
        .getChildFile("track-settings.json");
}

juce::File TrackSettingsStore::getLegacyGainsFile()
{
    return getDefaultFile().getSiblingFile("track-gains.json");
}

void TrackSettingsStore::setFile(const juce::File& file)
{
    storeFile = file;
}

juce::String TrackSettingsStore::keyFor(const juce::File& file)
{
    // Lowercased: Windows paths are case-insensitive, and the same track
    // reached through two differently-cased paths must not end up with
    // two different settings.
    return file.getFullPathName().toLowerCase();
}

void TrackSettingsStore::load()
{
    settingsByPath.clear();
    loadWarnings.clear();
    fileMustNotBeOverwritten = false;

    // Fall back to the file this replaced, so trims set in beta.7 survive
    // the upgrade. The old file is left alone; the first change writes
    // the new one.
    auto fileToRead = storeFile;
    if (!fileToRead.existsAsFile())
    {
        auto legacy = storeFile.getSiblingFile("track-gains.json");
        if (legacy.existsAsFile())
            fileToRead = legacy;
    }

    if (!fileToRead.existsAsFile())
        return;

    auto parsed = juce::JSON::parse(fileToRead.loadFileAsString());
    if (!parsed.isObject())
    {
        loadWarnings.add(fileToRead.getFileName() + " could not be read (not valid JSON) - "
                          "track volumes and fades have been reset to normal"
                          + juce::String(fileToRead == storeFile
                                             ? ". The file is being kept as it is, so volume and "
                                               "fade changes won't be saved until it's fixed"
                                             : ""));
        fileMustNotBeOverwritten = (fileToRead == storeFile);
        return;
    }

    if ((int) parsed.getProperty(kKeySchemaVersion, 0) > kCurrentSchemaVersion)
    {
        // Left strictly alone, same rule as the playlist and soundboard
        // files: rewriting a newer version's file with older code would
        // quietly destroy whatever it added.
        loadWarnings.add(fileToRead.getFileName() + " was made by a newer version of Inkwyrd Audio "
                          "and was not loaded"
                          + juce::String(fileToRead == storeFile
                                             ? ". It's being kept as it is, so volume and fade "
                                               "changes won't be saved in this version"
                                             : ""));
        fileMustNotBeOverwritten = (fileToRead == storeFile);
        return;
    }

    if (auto* tracks = parsed.getProperty(kKeyTracks, juce::var()).getDynamicObject())
    {
        for (const auto& property : tracks->getProperties())
        {
            const auto& entry = property.value;
            if (!entry.isObject())
                continue;

            TrackSettings settings;
            settings.gainDb = juce::jlimit(kMinDb, kMaxDb,
                                            (float) (double) entry.getProperty(kKeyGainDb, 0.0));
            settings.fadeSeconds = juce::jlimit(0.0, kMaxFadeSeconds,
                                                 (double) entry.getProperty(kKeyFadeSeconds, 0.0));

            if (!settings.isDefault())
                settingsByPath[property.name.toString().toLowerCase()] = settings;
        }
    }
    else if (auto* legacyGains = parsed.getProperty(kLegacyKeyGains, juce::var()).getDynamicObject())
    {
        for (const auto& property : legacyGains->getProperties())
        {
            TrackSettings settings;
            settings.gainDb = juce::jlimit(kMinDb, kMaxDb, (float) (double) property.value);

            if (!settings.isDefault())
                settingsByPath[property.name.toString().toLowerCase()] = settings;
        }
    }
}

const TrackSettingsStore::TrackSettings* TrackSettingsStore::find(const juce::File& file) const
{
    auto it = settingsByPath.find(keyFor(file));
    return it == settingsByPath.end() ? nullptr : &it->second;
}

void TrackSettingsStore::update(const juce::File& file, TrackSettings updated)
{
    auto key = keyFor(file);

    if (updated.isDefault())
        settingsByPath.erase(key); // back to normal - don't store a no-op
    else
        settingsByPath[key] = updated;

    save();
}

bool TrackSettingsStore::hasGain(const juce::File& file) const
{
    auto* settings = find(file);
    return settings != nullptr && !juce::approximatelyEqual(settings->gainDb, 0.0f);
}

float TrackSettingsStore::getGainDb(const juce::File& file) const
{
    auto* settings = find(file);
    return settings == nullptr ? 0.0f : settings->gainDb;
}

float TrackSettingsStore::getLinearGain(const juce::File& file) const
{
    auto db = getGainDb(file);
    return db == 0.0f ? 1.0f : juce::Decibels::decibelsToGain(db);
}

void TrackSettingsStore::setGainDb(const juce::File& file, float db)
{
    auto* existing = find(file);
    TrackSettings updated = existing != nullptr ? *existing : TrackSettings();
    updated.gainDb = juce::jlimit(kMinDb, kMaxDb, db);
    update(file, updated);
}

bool TrackSettingsStore::hasFade(const juce::File& file) const
{
    auto* settings = find(file);
    return settings != nullptr && !juce::approximatelyEqual(settings->fadeSeconds, 0.0);
}

double TrackSettingsStore::getFadeSeconds(const juce::File& file) const
{
    auto* settings = find(file);
    return settings == nullptr ? 0.0 : settings->fadeSeconds;
}

void TrackSettingsStore::setFadeSeconds(const juce::File& file, double seconds)
{
    auto* existing = find(file);
    TrackSettings updated = existing != nullptr ? *existing : TrackSettings();
    updated.fadeSeconds = juce::jlimit(0.0, kMaxFadeSeconds, seconds);
    update(file, updated);
}

void TrackSettingsStore::save()
{
    if (fileMustNotBeOverwritten)
        return;

    storeFile.getParentDirectory().createDirectory();

    juce::DynamicObject::Ptr tracks = new juce::DynamicObject();
    for (const auto& entry : settingsByPath)
    {
        juce::DynamicObject::Ptr object = new juce::DynamicObject();

        // Only what differs from normal, so the file stays readable and a
        // track that only has a trim doesn't claim a fade of zero.
        if (!juce::approximatelyEqual(entry.second.gainDb, 0.0f))
            object->setProperty(kKeyGainDb, (double) entry.second.gainDb);

        if (!juce::approximatelyEqual(entry.second.fadeSeconds, 0.0))
            object->setProperty(kKeyFadeSeconds, entry.second.fadeSeconds);

        tracks->setProperty(entry.first, juce::var(object.get()));
    }

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty(kKeySchemaVersion, kCurrentSchemaVersion);
    root->setProperty(kKeyTracks, juce::var(tracks.get()));

    // Atomic, same reasoning as the playlist and soundboard files.
    juce::TemporaryFile temp(storeFile);
    if (temp.getFile().replaceWithText(juce::JSON::toString(juce::var(root.get()), false)))
        temp.overwriteTargetFileWithTemporary();
}
