#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h> // MessageManager::callAsync, for reporting scan progress

// Embedded tags - title, artist, album, genre, year, track number - for
// every file in the track library, cached so the UI never touches the
// disk to paint a row.
//
// WHY THE WINDOWS PROPERTY SYSTEM RATHER THAN TAG PARSERS. This app's
// MP3 support is dr_mp3, which decodes audio and knows nothing about
// ID3; JUCE's own metadataValues only covers the formats whose readers
// bother (FLAC, Ogg, some WAV). Measured against the real library this
// was built for: 70 of 73 tracks are MP3, so "what JUCE gives us" was
// close to nothing. The alternatives were to write an ID3v2 parser (and
// later an MP4 atom parser, and a WMA one), or to ask Windows, which
// already has a property handler for every format the app can play and
// returned complete tags for all of them. Windows-only is not a new
// constraint here - the app already requires WASAPI and Media
// Foundation.
//
// JUCE's reader is still tried FIRST, because it's in-process and needs
// no COM; the property store is the fallback that actually does the work
// for MP3.
//
// Reads are cached to disk and invalidated by file size and modification
// time, so a re-scan costs nothing for files that haven't changed, and
// notices the ones that have.
struct TrackMetadata
{
    juce::String title, artist, album, albumArtist, genre;
    int year = 0;
    int trackNumber = 0;

    // False until a scan has actually looked at this file. Distinct from
    // "scanned and found nothing", which is a real answer and shouldn't
    // trigger a re-read every time the window opens.
    bool scanned = false;

    // What to show in a Title column. Falls back to the filename, which
    // is what the app displayed everywhere before tags existed.
    juce::String displayTitle(const juce::File& file) const;

    // Sort keys. Empty tags sort LAST rather than first - a column of
    // blanks at the top is the least useful possible ordering.
    juce::String sortKeyFor(const juce::String& value) const;
};

class TrackMetadataStore
{
public:
    TrackMetadataStore();
    ~TrackMetadataStore();

    static juce::File getDefaultFile();
    void setFile(const juce::File& file);

    void load();
    void save();

    // Message thread. Never blocks and never reads a file - anything not
    // scanned yet comes back with `scanned == false`.
    TrackMetadata get(const juce::File& file) const;

    // Scans anything missing or stale on a background thread. onProgress
    // fires on the MESSAGE thread every so often so a list can repaint as
    // results arrive rather than sitting blank until the end; onFinished
    // fires once, also on the message thread.
    //
    // Calling this again while a scan is running cancels the old one -
    // the library changing mid-scan means the old work list is already
    // wrong.
    void scanAsync(const juce::Array<juce::File>& files,
                    std::function<void()> onProgress,
                    std::function<void()> onFinished);

    void cancelScan();
    bool isScanning() const;

    // How many files the running scan has left. 0 when idle.
    int getRemainingCount() const { return remaining.load(); }

    static constexpr int kCurrentSchemaVersion = 1;

    // Reads one file synchronously. Public so a headless test can check
    // extraction without going near the cache or a thread.
    static TrackMetadata readFromFile(const juce::File& file,
                                       juce::AudioFormatManager& formatManager);

private:
    class ScanThread;

    struct Entry
    {
        TrackMetadata metadata;
        juce::int64 fileSize = 0;
        juce::int64 modifiedMs = 0;
    };

    static juce::String keyFor(const juce::File& file);
    bool isStale(const juce::File& file, const Entry& entry) const;

    juce::File storeFile;
    std::map<juce::String, Entry> entries;

    // Guards `entries`: the scan thread writes results into it while the
    // message thread reads them to paint.
    mutable juce::CriticalSection lock;

    // Set when the cache on disk came from a NEWER version: save() then
    // refuses, rather than rewriting it in this version's older shape.
    // Deliberately NOT set for invalid JSON, unlike the other stores -
    // this file is only a cache of tags read from the tracks themselves,
    // so replacing a corrupt one loses nothing, and refusing would stop
    // it ever rebuilding. Atomic because the scan thread saves too.
    std::atomic<bool> fileMustNotBeOverwritten { false };

    std::unique_ptr<ScanThread> scanThread;
    std::atomic<int> remaining { 0 };

    juce::AudioFormatManager formatManager;
};
