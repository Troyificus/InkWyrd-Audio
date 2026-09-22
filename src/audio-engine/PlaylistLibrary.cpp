#include "PlaylistLibrary.h"

#include <set>

namespace
{
    constexpr const char* kKeySchemaVersion = "schemaVersion";
    constexpr const char* kKeyId = "id";
    constexpr const char* kKeyName = "name";
    constexpr const char* kKeyShuffle = "shuffle";
    constexpr const char* kKeyEntries = "entries";
    constexpr const char* kKeyKind = "kind";
    constexpr const char* kKeyPath = "path";
    constexpr const char* kKeyMode = "mode";
    constexpr const char* kKeyRecursive = "recursive";
    constexpr const char* kKeyTracks = "tracks";

    constexpr const char* kKindFile = "file";
    constexpr const char* kKindFolder = "folder";
    constexpr const char* kModeLive = "live";
    constexpr const char* kModeSnapshot = "snapshot";
}

namespace inkwyrd
{
    juce::Array<juce::File> scanFolderForAudio(const juce::File& folder,
                                                juce::AudioFormatManager& formatManager,
                                                bool recursive)
    {
        juce::Array<juce::File> found;
        if (!folder.isDirectory())
            return found;

        for (const auto& entry : juce::RangedDirectoryIterator(folder, recursive, "*", juce::File::findFiles))
        {
            auto file = entry.getFile();
            if (formatManager.findFormatForFileExtension(file.getFileExtension()) != nullptr)
                found.add(file);
        }

        // RangedDirectoryIterator's order is filesystem-dependent. Sort so
        // an unshuffled playlist plays in a predictable, repeatable order.
        found.sort();
        return found;
    }
}

PlaylistLibrary::PlaylistLibrary(juce::AudioFormatManager& formatManagerToUse)
    : formatManager(formatManagerToUse), playlistDirectory(getDefaultDirectory())
{
}

juce::File PlaylistLibrary::getDefaultDirectory()
{
    // Beside the existing "Inkwyrd Audio.settings" file, so everything the
    // app persists lives in one discoverable place.
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Inkwyrd Audio")
        .getChildFile("Playlists");
}

void PlaylistLibrary::setDirectory(const juce::File& directory)
{
    playlistDirectory = directory;
}

void PlaylistLibrary::loadAll()
{
    playlists.clear();
    loadWarnings.clear();

    if (!playlistDirectory.isDirectory())
        return;

    juce::Array<juce::File> files;
    for (const auto& entry : juce::RangedDirectoryIterator(playlistDirectory, false, "*.json", juce::File::findFiles))
        files.add(entry.getFile());
    files.sort();

    for (const auto& file : files)
    {
        auto loaded = std::make_unique<Playlist>();
        juce::String warning;

        if (readPlaylistFile(file, *loaded, warning))
            playlists.add(loaded.release());
        else if (warning.isNotEmpty())
            loadWarnings.add(warning);
    }
}

bool PlaylistLibrary::readPlaylistFile(const juce::File& file, Playlist& outPlaylist, juce::String& outWarning) const
{
    auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (!parsed.isObject())
    {
        outWarning = file.getFileName() + " could not be read (not valid JSON)";
        return false;
    }

    auto schemaVersion = (int) parsed.getProperty(kKeySchemaVersion, 0);
    if (schemaVersion > kCurrentSchemaVersion)
    {
        // Leave it strictly alone. Rewriting a file from a newer version
        // with older code would quietly destroy whatever it added.
        outWarning = file.getFileName() + " was made by a newer version of Inkwyrd Audio and was skipped";
        return false;
    }

    outPlaylist.sourceFile = file;
    outPlaylist.name = parsed.getProperty(kKeyName, file.getFileNameWithoutExtension()).toString();
    outPlaylist.shuffle = (bool) parsed.getProperty(kKeyShuffle, true);

    auto idString = parsed.getProperty(kKeyId, "").toString();
    outPlaylist.id = idString.isNotEmpty() ? juce::Uuid(idString) : juce::Uuid();

    if (auto* entryArray = parsed.getProperty(kKeyEntries, juce::var()).getArray())
    {
        for (const auto& entryVar : *entryArray)
        {
            if (!entryVar.isObject())
                continue;

            PlaylistEntry entry;
            auto kind = entryVar.getProperty(kKeyKind, "").toString();
            auto path = entryVar.getProperty(kKeyPath, "").toString();
            if (path.isEmpty())
                continue;

            entry.path = juce::File(path);

            if (kind == kKindFile)
            {
                entry.kind = PlaylistEntry::Kind::singleFile;
            }
            else if (kind == kKindFolder)
            {
                entry.kind = PlaylistEntry::Kind::folder;
                entry.recursive = (bool) entryVar.getProperty(kKeyRecursive, true);
                entry.live = entryVar.getProperty(kKeyMode, kModeLive).toString() != kModeSnapshot;

                if (!entry.live)
                    if (auto* trackArray = entryVar.getProperty(kKeyTracks, juce::var()).getArray())
                        for (const auto& trackVar : *trackArray)
                            entry.snapshot.add(juce::File(trackVar.toString()));
            }
            else
            {
                continue; // unknown kind - skip the entry, keep the playlist
            }

            outPlaylist.entries.add(entry);
        }
    }

    return true;
}

Playlist* PlaylistLibrary::getPlaylist(int index)
{
    return playlists[index];
}

Playlist* PlaylistLibrary::findById(const juce::Uuid& id)
{
    for (auto* p : playlists)
        if (p->id == id)
            return p;
    return nullptr;
}

Playlist* PlaylistLibrary::findByName(const juce::String& name)
{
    for (auto* p : playlists)
        if (p->name.equalsIgnoreCase(name))
            return p;
    return nullptr;
}

juce::String PlaylistLibrary::makeUniqueName(const juce::String& desiredName) const
{
    auto base = desiredName.trim();
    if (base.isEmpty())
        base = "Untitled";

    auto candidate = base;
    int suffix = 2;
    while (true)
    {
        bool taken = false;
        for (auto* p : playlists)
            if (p->name.equalsIgnoreCase(candidate))
                taken = true;

        if (!taken)
            return candidate;

        candidate = base + " (" + juce::String(suffix++) + ")";
    }
}

Playlist& PlaylistLibrary::createPlaylist(const juce::String& desiredName)
{
    auto* playlist = playlists.add(new Playlist());
    playlist->name = makeUniqueName(desiredName);
    save(*playlist);
    return *playlist;
}

Playlist& PlaylistLibrary::createFromLegacyFolder(const juce::File& folder)
{
    auto& playlist = createPlaylist(folder.getFileName());

    PlaylistEntry entry;
    entry.kind = PlaylistEntry::Kind::folder;
    entry.path = folder;
    entry.live = true;
    entry.recursive = true; // exactly what the old single-folder loadFolder did
    playlist.entries.add(entry);

    save(playlist);
    return playlist;
}

bool PlaylistLibrary::renamePlaylist(const juce::Uuid& id, const juce::String& newName)
{
    auto* playlist = findById(id);
    if (playlist == nullptr)
        return false;

    auto trimmed = newName.trim();
    if (trimmed.isEmpty())
        return false;

    if (auto* existing = findByName(trimmed))
        if (existing != playlist)
            return false; // name already taken by a different playlist

    auto oldFile = playlist->sourceFile;
    playlist->name = trimmed;

    // Try to keep the filename readable, but the name inside the JSON is
    // what's authoritative - if the move fails, the old filename is
    // harmless and the playlist is still correct.
    auto desired = chooseFileFor(*playlist);
    if (oldFile.existsAsFile() && desired != oldFile)
        if (oldFile.moveFileTo(desired))
            playlist->sourceFile = desired;

    save(*playlist);
    return true;
}

void PlaylistLibrary::deletePlaylist(const juce::Uuid& id)
{
    for (int i = playlists.size(); --i >= 0;)
    {
        if (playlists[i]->id != id)
            continue;

        auto file = playlists[i]->sourceFile;
        if (file.existsAsFile())
            if (!file.moveToTrash()) // recoverable, unlike deleteFile()
                file.deleteFile();

        playlists.remove(i);
        return;
    }
}

bool PlaylistLibrary::isPlayableFile(const juce::File& file) const
{
    return formatManager.findFormatForFileExtension(file.getFileExtension()) != nullptr;
}

juce::Array<juce::File> PlaylistLibrary::scanFolder(const juce::File& folder, bool recursive)
{
    return inkwyrd::scanFolderForAudio(folder, formatManager, recursive);
}

void PlaylistLibrary::addFiles(const juce::Uuid& id, const juce::Array<juce::File>& files)
{
    auto* playlist = findById(id);
    if (playlist == nullptr)
        return;

    for (const auto& file : files)
    {
        if (!isPlayableFile(file))
            continue;

        PlaylistEntry entry;
        entry.kind = PlaylistEntry::Kind::singleFile;
        entry.path = file;
        playlist->entries.add(entry);
    }

    save(*playlist);
}

void PlaylistLibrary::addFolderLink(const juce::Uuid& id, const juce::File& folder, bool recursive)
{
    auto* playlist = findById(id);
    if (playlist == nullptr)
        return;

    PlaylistEntry entry;
    entry.kind = PlaylistEntry::Kind::folder;
    entry.path = folder;
    entry.live = true;
    entry.recursive = recursive;
    playlist->entries.add(entry);

    save(*playlist);
}

void PlaylistLibrary::addFolderSnapshot(const juce::Uuid& id, const juce::File& folder, bool recursive)
{
    auto* playlist = findById(id);
    if (playlist == nullptr)
        return;

    PlaylistEntry entry;
    entry.kind = PlaylistEntry::Kind::folder;
    entry.path = folder;
    entry.live = false;
    entry.recursive = recursive;
    entry.snapshot = inkwyrd::scanFolderForAudio(folder, formatManager, recursive);
    playlist->entries.add(entry);

    save(*playlist);
}

void PlaylistLibrary::removeEntry(const juce::Uuid& id, int entryIndex)
{
    auto* playlist = findById(id);
    if (playlist == nullptr || !juce::isPositiveAndBelow(entryIndex, playlist->entries.size()))
        return;

    playlist->entries.remove(entryIndex);
    save(*playlist);
}

void PlaylistLibrary::removeEntries(const juce::Uuid& id, juce::Array<int> entryIndices)
{
    auto* playlist = findById(id);
    if (playlist == nullptr || entryIndices.isEmpty())
        return;

    // Highest first, so each removal leaves the indices still to come
    // pointing at the entries they meant.
    entryIndices.sort();
    auto removedAny = false;

    for (int i = entryIndices.size(); --i >= 0;)
    {
        auto index = entryIndices[i];
        if (i + 1 < entryIndices.size() && entryIndices[i + 1] == index)
            continue; // a repeat - already gone

        if (juce::isPositiveAndBelow(index, playlist->entries.size()))
        {
            playlist->entries.remove(index);
            removedAny = true;
        }
    }

    if (removedAny)
        save(*playlist);
}

bool PlaylistLibrary::moveEntriesTo(const juce::Uuid& id, juce::Array<int> entryIndices, int targetIndex)
{
    auto* playlist = findById(id);
    if (playlist == nullptr || entryIndices.isEmpty())
        return false;

    entryIndices.sort();

    // juce::Array has no removeDuplicates of its own; sorted, a repeat is
    // always the entry before.
    for (int i = entryIndices.size(); --i > 0;)
        if (entryIndices[i] == entryIndices[i - 1])
            entryIndices.remove(i);

    for (auto index : entryIndices)
        if (! juce::isPositiveAndBelow(index, playlist->entries.size()))
            return false;

    targetIndex = juce::jlimit(0, playlist->entries.size(), targetIndex);

    // Where the block lands once the entries being moved are out of the
    // list: every one of them below the target shifts it up by one.
    auto insertAt = targetIndex;
    for (auto index : entryIndices)
        if (index < targetIndex)
            --insertAt;

    // Already exactly there: a contiguous block whose first entry is
    // already at the insertion point has nothing to do, and moving it
    // anyway would rewrite the file for no change.
    auto contiguous = entryIndices.getLast() - entryIndices.getFirst() == entryIndices.size() - 1;
    if (contiguous && entryIndices.getFirst() == insertAt)
        return false;

    juce::Array<PlaylistEntry> moving;
    for (auto index : entryIndices)
        moving.add(playlist->entries.getReference(index));

    for (int i = entryIndices.size(); --i >= 0;)
        playlist->entries.remove(entryIndices[i]);

    for (int i = 0; i < moving.size(); ++i)
        playlist->entries.insert(insertAt + i, moving.getReference(i));

    save(*playlist);
    return true;
}

bool PlaylistLibrary::moveEntriesBy(const juce::Uuid& id, juce::Array<int> entryIndices, int delta)
{
    auto* playlist = findById(id);
    if (playlist == nullptr || entryIndices.isEmpty() || delta == 0)
        return false;

    entryIndices.sort();

    // juce::Array has no removeDuplicates of its own; sorted, a repeat is
    // always the entry before.
    for (int i = entryIndices.size(); --i > 0;)
        if (entryIndices[i] == entryIndices[i - 1])
            entryIndices.remove(i);

    if (delta < 0)
    {
        if (entryIndices.getFirst() == 0)
            return false; // already at the top

        return moveEntriesTo(id, entryIndices, entryIndices.getFirst() - 1);
    }

    if (entryIndices.getLast() >= playlist->entries.size() - 1)
        return false; // already at the bottom

    // Past the entry below the block: +2 because the target is "before
    // the entry at this index", and the block itself is still in the way.
    return moveEntriesTo(id, entryIndices, entryIndices.getLast() + 2);
}

void PlaylistLibrary::setShuffle(const juce::Uuid& id, bool shouldShuffle)
{
    auto* playlist = findById(id);
    if (playlist == nullptr || playlist->shuffle == shouldShuffle)
        return;

    playlist->shuffle = shouldShuffle;
    save(*playlist);
}

juce::Array<juce::File> PlaylistLibrary::tracksLinkedToFolder(const juce::File& folder) const
{
    juce::Array<juce::File> tracks;

    for (const auto* playlist : playlists)
    {
        auto resolved = resolve(*playlist);

        for (int i = 0; i < resolved.files.size(); ++i)
        {
            auto entryIndex = resolved.sourceEntryIndex[i];
            if (! juce::isPositiveAndBelow(entryIndex, playlist->entries.size()))
                continue;

            const auto& entry = playlist->entries.getReference(entryIndex);
            if (entry.kind == PlaylistEntry::Kind::folder && entry.path == folder)
                tracks.addIfNotAlreadyThere(resolved.files[i]);
        }
    }

    return tracks;
}

ResolvedPlaylist PlaylistLibrary::resolve(const Playlist& playlist) const
{
    ResolvedPlaylist resolved;
    std::set<juce::String> seen; // dedup: a file can be both added directly and inside a linked folder

    auto addFile = [&](const juce::File& file, int entryIndex)
    {
        auto key = file.getFullPathName().toLowerCase();
        if (!seen.insert(key).second)
            return;

        resolved.files.add(file);
        resolved.sourceEntryIndex.add(entryIndex);
    };

    for (int i = 0; i < playlist.entries.size(); ++i)
    {
        const auto& entry = playlist.entries.getReference(i);

        if (entry.kind == PlaylistEntry::Kind::singleFile)
        {
            if (entry.path.existsAsFile())
                addFile(entry.path, i);
            else
                resolved.missingPaths.add(entry.path.getFullPathName());

            continue;
        }

        if (entry.live)
        {
            if (!entry.path.isDirectory())
            {
                resolved.missingPaths.add(entry.path.getFullPathName());
                continue;
            }

            for (const auto& file : inkwyrd::scanFolderForAudio(entry.path,
                                                                 const_cast<juce::AudioFormatManager&>(formatManager),
                                                                 entry.recursive))
                addFile(file, i);
        }
        else
        {
            for (const auto& file : entry.snapshot)
            {
                if (file.existsAsFile())
                    addFile(file, i);
                else
                    resolved.missingPaths.add(file.getFullPathName());
            }
        }
    }

    return resolved;
}

juce::File PlaylistLibrary::chooseFileFor(const Playlist& playlist) const
{
    auto slug = juce::File::createLegalFileName(playlist.name).trim();
    if (slug.isEmpty())
        slug = "playlist";

    auto candidate = playlistDirectory.getChildFile(slug + ".json");
    int suffix = 2;
    while (candidate.existsAsFile() && candidate != playlist.sourceFile)
        candidate = playlistDirectory.getChildFile(slug + "-" + juce::String(suffix++) + ".json");

    return candidate;
}

void PlaylistLibrary::save(const Playlist& playlist)
{
    playlistDirectory.createDirectory();

    auto& mutablePlaylist = const_cast<Playlist&>(playlist);
    if (mutablePlaylist.sourceFile == juce::File())
        mutablePlaylist.sourceFile = chooseFileFor(playlist);

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty(kKeySchemaVersion, kCurrentSchemaVersion);
    root->setProperty(kKeyId, playlist.id.toDashedString());
    root->setProperty(kKeyName, playlist.name);
    root->setProperty(kKeyShuffle, playlist.shuffle);

    juce::Array<juce::var> entryVars;
    for (const auto& entry : playlist.entries)
    {
        juce::DynamicObject::Ptr entryObject = new juce::DynamicObject();
        entryObject->setProperty(kKeyPath, entry.path.getFullPathName());

        if (entry.kind == PlaylistEntry::Kind::singleFile)
        {
            entryObject->setProperty(kKeyKind, kKindFile);
        }
        else
        {
            entryObject->setProperty(kKeyKind, kKindFolder);
            entryObject->setProperty(kKeyMode, entry.live ? kModeLive : kModeSnapshot);
            entryObject->setProperty(kKeyRecursive, entry.recursive);

            if (!entry.live)
            {
                juce::Array<juce::var> trackVars;
                for (const auto& file : entry.snapshot)
                    trackVars.add(file.getFullPathName());
                entryObject->setProperty(kKeyTracks, juce::var(trackVars));
            }
        }

        entryVars.add(juce::var(entryObject.get()));
    }
    root->setProperty(kKeyEntries, juce::var(entryVars));

    // Multi-line: this folder is meant to be opened and read by humans.
    auto json = juce::JSON::toString(juce::var(root.get()), false);

    // Atomic: a half-written playlist left by a crash mid-save would be
    // unrecoverable, and these are the user's own curated lists.
    juce::TemporaryFile temp(mutablePlaylist.sourceFile);
    if (temp.getFile().replaceWithText(json))
        temp.overwriteTargetFileWithTemporary();
}

void PlaylistLibrary::saveAll()
{
    for (auto* playlist : playlists)
        save(*playlist);
}
