#pragma once

#include <map>

#include <juce_audio_basics/juce_audio_basics.h> // juce::Decibels
#include <juce_core/juce_core.h>

// Per-track settings - a volume trim and a crossfade length - persisted
// to %APPDATA%\Inkwyrd Audio\track-settings.json.
//
// Keyed by the FILE, not by (playlist, file). A track exported hotter
// than everything else is loud wherever it appears, and a track that ends
// on a long tail needs the same treatment wherever it appears, so both
// belong to the track rather than to the list it happens to be in. That
// also means a folder-linked playlist - which has no per-track rows of
// its own to hang a setting off - gets both for free.
//
// This is deliberately NOT a "wiring" system linking specific pairs of
// tracks with specific transitions. Playlists shuffle by default, so a
// pairing mostly never comes up, and making one authoritative would mean
// a second ordering system competing with shuffle. A fade length per
// track survives shuffle: whatever plays next, each track leaves the way
// it was told to. For a genuinely fixed sequence, a playlist with shuffle
// turned off already does the job.
//
// Defaults are stored as "no entry at all", so an untouched library has
// an empty file and the common case costs nothing.
class TrackSettingsStore
{
public:
    static juce::File getDefaultFile();

    // The file this replaced, read once if the current one is absent, so
    // trims set in beta.7 aren't silently lost on upgrade.
    static juce::File getLegacyGainsFile();

    // Overridable so tests can point at a scratch file.
    void setFile(const juce::File& file);
    juce::File getFile() const { return storeFile; }

    void load();
    juce::StringArray getLoadWarnings() const { return loadWarnings; }

    // A trim, not a fader: the useful range is "pull this one down a bit"
    // with a little headroom to push a quiet track up.
    static constexpr float kMinDb = -24.0f;
    static constexpr float kMaxDb = 6.0f;

    float getGainDb(const juce::File& file) const;      // 0 if never set
    float getLinearGain(const juce::File& file) const;  // 1 if never set
    bool hasGain(const juce::File& file) const;

    // Saves immediately. Setting 0 dB REMOVES the trim rather than
    // storing a no-op.
    void setGainDb(const juce::File& file, float db);

    // How long this track takes to fade into whatever follows it. 0 means
    // "use whatever the global crossfade length is", which is what every
    // track does until told otherwise.
    static constexpr double kMaxFadeSeconds = 15.0;

    double getFadeSeconds(const juce::File& file) const; // 0 = follow the global setting
    bool hasFade(const juce::File& file) const;
    void setFadeSeconds(const juce::File& file, double seconds);

    int getNumEntries() const { return (int) settingsByPath.size(); }

    void save();

    static constexpr int kCurrentSchemaVersion = 1;

private:
    struct TrackSettings
    {
        float gainDb = 0.0f;
        double fadeSeconds = 0.0;

        bool isDefault() const
        {
            return juce::approximatelyEqual(gainDb, 0.0f)
                    && juce::approximatelyEqual(fadeSeconds, 0.0);
        }
    };

    static juce::String keyFor(const juce::File& file);

    const TrackSettings* find(const juce::File& file) const;
    void update(const juce::File& file, TrackSettings updated);

    juce::File storeFile;
    std::map<juce::String, TrackSettings> settingsByPath;
    juce::StringArray loadWarnings;
};
