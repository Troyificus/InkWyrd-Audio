#pragma once

#include <map>

#include <juce_audio_basics/juce_audio_basics.h> // juce::Decibels
#include <juce_core/juce_core.h>

// Per-track volume trim, in dB, persisted to
// %APPDATA%\Inkwyrd Audio\track-gains.json.
//
// Keyed by the FILE, not by (playlist, file): a track that was exported
// hotter than everything else is loud wherever it appears, so turning it
// down once should fix it in every playlist that contains it. That also
// means a folder-linked playlist - which has no per-track rows of its own
// to hang a setting off - gets trims for free.
//
// 0 dB is stored as "no entry at all", so an untouched library has an
// empty file and the common case costs nothing.
class TrackGainStore
{
public:
    static juce::File getDefaultFile();

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

    // Saves immediately. Setting 0 dB REMOVES the entry rather than
    // storing a no-op.
    void setGainDb(const juce::File& file, float db);

    int getNumEntries() const { return (int) gainsByPath.size(); }

    void save();

    static constexpr int kCurrentSchemaVersion = 1;

private:
    static juce::String keyFor(const juce::File& file);

    juce::File storeFile;
    std::map<juce::String, float> gainsByPath;
    juce::StringArray loadWarnings;
};
