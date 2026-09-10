#pragma once

#include <map>

#include <juce_core/juce_core.h>

// The master list of every track added to the app, persisted to
// %APPDATA%\Inkwyrd Audio\track-library.json.
//
// This is genuinely new ground rather than a view over existing data.
// PlaylistLibrary is deliberately "a playlist is a list of ENTRIES, not a
// flat list of files" (its own header says so), so before this class a
// track existed ONLY as an entry inside one particular playlist: add the
// same file to three playlists and there were three unrelated records of
// it and no way to ask "what music does this app know about?".
//
// Modelled on TrackSettingsStore (one flat table keyed by lowercased
// path) rather than PlaylistLibrary (one file per named document),
// because that's the shape of the thing: a single set, not a collection
// of documents.
//
// Membership here is INDEPENDENT of playlist membership in both
// directions. Adding a file to a playlist registers it here too, but
// removing it from this library does NOT touch any playlist that
// references it - a playlist entry is its own record, and silently
// editing someone's playlists as a side effect of tidying a browse list
// would be a nasty surprise. A track can therefore disappear from "all
// tracks" while still playing perfectly inside a playlist that has it.
class TrackLibrary
{
public:
    static juce::File getDefaultFile();

    // Overridable so tests can point at a scratch file.
    void setFile(const juce::File& file);
    juce::File getFile() const { return storeFile; }

    void load();
    void save();
    juce::StringArray getLoadWarnings() const { return loadWarnings; }

    // Idempotent - registering a file already present is a no-op, so
    // callers never have to check first. Returns how many were actually
    // new, which is what makes a migration able to report itself.
    bool registerTrack(const juce::File& file);
    int registerTracks(const juce::Array<juce::File>& files);

    // Forget a track. Playlists are untouched - see the note above.
    void removeTrack(const juce::File& file);

    bool contains(const juce::File& file) const;
    int getNumTracks() const { return (int) entriesByPath.size(); }

    // Sorted by filename (case-insensitive) so the browse list has a
    // stable, findable order rather than insertion order.
    juce::Array<juce::File> getAllTracks() const;

    static constexpr int kCurrentSchemaVersion = 1;

private:
    // Lowercased full path, matching TrackSettingsStore's own key rule -
    // Windows paths are case-insensitive, so the same file reached two
    // ways must not become two rows.
    static juce::String keyFor(const juce::File& file);

    juce::File storeFile;
    std::map<juce::String, juce::File> entriesByPath;
    juce::StringArray loadWarnings;
};
