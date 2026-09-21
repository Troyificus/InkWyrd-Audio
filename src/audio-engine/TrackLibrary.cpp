#include "TrackLibrary.h"

namespace
{
    constexpr const char* kKeySchemaVersion = "schemaVersion";
    constexpr const char* kKeyTracks = "tracks";
}

juce::File TrackLibrary::getDefaultFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Inkwyrd Audio")
        .getChildFile("track-library.json");
}

void TrackLibrary::setFile(const juce::File& file)
{
    storeFile = file;
}

juce::String TrackLibrary::keyFor(const juce::File& file)
{
    return file.getFullPathName().toLowerCase();
}

void TrackLibrary::load()
{
    entriesByPath.clear();
    loadWarnings.clear();
    fileMustNotBeOverwritten = false;

    if (! storeFile.existsAsFile())
        return; // a first run has no library yet; that's not a problem

    auto parsed = juce::JSON::parse(storeFile.loadFileAsString());
    auto* root = parsed.getDynamicObject();

    if (root == nullptr)
    {
        loadWarnings.add("The track library file couldn't be read, so the library is empty. The file "
                          "is being kept as it is, so changes to the library won't be saved until "
                          "it's fixed: " + storeFile.getFullPathName());
        fileMustNotBeOverwritten = true;
        return;
    }

    // Same discipline as every other store here: a file written by a
    // NEWER version is left alone and reported, never partially read and
    // then written back in an older shape.
    auto schemaVersion = (int) root->getProperty(kKeySchemaVersion);
    if (schemaVersion > kCurrentSchemaVersion)
    {
        loadWarnings.add("The track library was written by a newer version of Inkwyrd Audio and "
                          "was not loaded. It's being kept as it is, so changes to the library "
                          "won't be saved in this version.");
        fileMustNotBeOverwritten = true;
        return;
    }

    if (auto* tracks = root->getProperty(kKeyTracks).getArray())
    {
        for (const auto& entry : *tracks)
        {
            juce::File file(entry.toString());
            if (file.getFullPathName().isNotEmpty())
                entriesByPath[keyFor(file)] = file;
        }
    }
}

void TrackLibrary::save()
{
    if (fileMustNotBeOverwritten)
        return;

    storeFile.getParentDirectory().createDirectory();

    juce::Array<juce::var> tracks;
    for (const auto& entry : entriesByPath)
        tracks.add(entry.second.getFullPathName());

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty(kKeySchemaVersion, kCurrentSchemaVersion);
    root->setProperty(kKeyTracks, juce::var(tracks));

    // Atomic, same reasoning as the playlist, soundboard and
    // track-settings files: a half-written library from a crash would be
    // unrecoverable.
    juce::TemporaryFile temp(storeFile);
    if (temp.getFile().replaceWithText(juce::JSON::toString(juce::var(root.get()), false)))
        temp.overwriteTargetFileWithTemporary();
}

bool TrackLibrary::registerTrack(const juce::File& file)
{
    if (file.getFullPathName().isEmpty())
        return false;

    auto key = keyFor(file);
    if (entriesByPath.find(key) != entriesByPath.end())
        return false;

    entriesByPath[key] = file;
    return true;
}

int TrackLibrary::registerTracks(const juce::Array<juce::File>& files)
{
    int added = 0;

    for (const auto& file : files)
        if (registerTrack(file))
            ++added;

    if (added > 0)
    {
        save();

        if (onTracksAdded)
            onTracksAdded();
    }

    return added;
}

void TrackLibrary::removeTrack(const juce::File& file)
{
    if (entriesByPath.erase(keyFor(file)) > 0)
        save();
}

bool TrackLibrary::contains(const juce::File& file) const
{
    return entriesByPath.find(keyFor(file)) != entriesByPath.end();
}

juce::Array<juce::File> TrackLibrary::getAllTracks() const
{
    juce::Array<juce::File> result;
    for (const auto& entry : entriesByPath)
        result.add(entry.second);

    // By filename rather than full path: the browse list is read by
    // track name, and sorting by path would scatter the same album
    // across the list depending on which folder it came from.
    std::sort(result.begin(), result.end(), [](const juce::File& a, const juce::File& b)
    {
        auto byName = a.getFileNameWithoutExtension()
                        .compareIgnoreCase(b.getFileNameWithoutExtension());
        return byName != 0 ? byName < 0
                            : a.getFullPathName().compareIgnoreCase(b.getFullPathName()) < 0;
    });

    return result;
}
