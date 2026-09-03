#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

// Multiple named playlists, each persisted as its own JSON file under
// %APPDATA%\Inkwyrd Audio\Playlists\. One file per playlist (rather than
// everything inside the settings XML) so they can be backed up, copied
// between machines, or handed to someone else individually.
//
// A playlist is a list of ENTRIES, not a flat list of files. An entry is
// either one specific file or a folder, and a folder entry is either
// "live" (re-scanned every time the playlist is used, so files added to
// that folder later show up) or a one-time snapshot (frozen at import,
// so individual tracks can be removed without reappearing). One playlist
// can mix all three.

namespace inkwyrd
{
    // Shared by PlaylistLibrary and PlaylistEngine::loadFolder so the two
    // can never disagree about what counts as a playable file.
    juce::Array<juce::File> scanFolderForAudio(const juce::File& folder,
                                                juce::AudioFormatManager& formatManager,
                                                bool recursive);
}

struct PlaylistEntry
{
    enum class Kind { singleFile, folder };

    Kind kind = Kind::singleFile;
    juce::File path;                  // the file itself, or the folder root
    bool live = true;                 // folder only: re-scan on every resolve
    bool recursive = true;            // folder only
    juce::Array<juce::File> snapshot; // folder + !live: the frozen file list
};

struct Playlist
{
    juce::Uuid id;    // stable across renames - this, not the name, is the key
    juce::String name;
    bool shuffle = true; // matches PlaylistEngine's own default
    juce::Array<PlaylistEntry> entries;
    juce::File sourceFile; // the .json this came from; {} until first saved
};

// The flat, playable track list an entry list boils down to.
struct ResolvedPlaylist
{
    juce::Array<juce::File> files;
    // Parallel to files: which entry produced each one, so the UI can offer
    // "remove this track" vs "remove the linked folder that brought it in".
    juce::Array<int> sourceEntryIndex;
    // Entries whose file/folder no longer exists. Reported, never silently
    // deleted - an unplugged drive must not destroy someone's playlist.
    juce::StringArray missingPaths;
};

class PlaylistLibrary
{
public:
    explicit PlaylistLibrary(juce::AudioFormatManager& formatManagerToUse);

    static juce::File getDefaultDirectory();

    // Overridable so tests can point the library at a scratch directory.
    void setDirectory(const juce::File& directory);
    juce::File getDirectory() const { return playlistDirectory; }

    void loadAll();

    // Files that couldn't be read, or were written by a newer version.
    // Surfaced in the UI rather than thrown away.
    juce::StringArray getLoadWarnings() const { return loadWarnings; }

    int getNumPlaylists() const { return playlists.size(); }
    bool isEmpty() const { return playlists.isEmpty(); }
    Playlist* getPlaylist(int index);
    Playlist* findById(const juce::Uuid& id);
    Playlist* findByName(const juce::String& name); // case-insensitive

    // Auto-suffixes " (2)", " (3)" etc. if the name is taken.
    Playlist& createPlaylist(const juce::String& desiredName);
    bool renamePlaylist(const juce::Uuid& id, const juce::String& newName);
    void deletePlaylist(const juce::Uuid& id);

    void addFiles(const juce::Uuid& id, const juce::Array<juce::File>& files);
    void addFolderLink(const juce::Uuid& id, const juce::File& folder, bool recursive);
    void addFolderSnapshot(const juce::Uuid& id, const juce::File& folder, bool recursive);
    void removeEntry(const juce::Uuid& id, int entryIndex);
    void setShuffle(const juce::Uuid& id, bool shouldShuffle);

    ResolvedPlaylist resolve(const Playlist& playlist) const;

    // Every mutator above saves immediately - these are rare, user-driven,
    // single small-file writes, and losing an edit to a crash would be
    // worse than the cost of writing.
    void save(const Playlist& playlist);
    void saveAll();

    // One-time migration for users upgrading from the single-folder
    // version: wraps their existing music folder as a live, recursive
    // folder link, which is exactly what the old loadFolder() did.
    Playlist& createFromLegacyFolder(const juce::File& folder);

    static constexpr int kCurrentSchemaVersion = 1;

private:
    juce::String makeUniqueName(const juce::String& desiredName) const;
    juce::File chooseFileFor(const Playlist& playlist) const;
    bool readPlaylistFile(const juce::File& file, Playlist& outPlaylist, juce::String& outWarning) const;

    juce::AudioFormatManager& formatManager;
    juce::File playlistDirectory;
    juce::OwnedArray<Playlist> playlists;
    juce::StringArray loadWarnings;
};
