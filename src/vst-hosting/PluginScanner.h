#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Finds VST3 plugins on disk and knows how to instantiate one from its
// description. VST3 only, per the design brief - no VST2 support.
class PluginScanner
{
public:
    PluginScanner();

    // Scans the standard system VST3 folder (and any extra folder
    // given) for plugins, deduplicating against what's already known.
    // Returns the descriptions found in this pass.
    //
    // SLOW - this loads every plugin on the machine to read its
    // description, measured at ~18 seconds for 40 plugins. Never call it
    // on the message thread: it blocks the entire UI for the duration.
    // See InkwyrdAudioApplication::startPluginScan().
    juce::Array<juce::PluginDescription> scan(const juce::File& extraFolder = {});

    // The scan result, persisted so it only has to be paid for once
    // rather than on every launch. restoreFromCache() is cheap (it
    // parses XML, it does not touch a single plugin binary).
    void restoreFromCache(const juce::File& cacheFile);
    void saveToCache(const juce::File& cacheFile) const;

    int getNumKnownPlugins() const { return knownPlugins.getNumTypes(); }

    juce::Array<juce::PluginDescription> getKnownPlugins() const;

    // Instantiates a plugin from a description found by scan(). Returns
    // nullptr (with errorMessage filled in) on failure - a plugin that
    // fails to load shouldn't take the whole host down.
    std::unique_ptr<juce::AudioPluginInstance> createInstance(const juce::PluginDescription& description,
                                                                double sampleRate,
                                                                int blockSize,
                                                                juce::String& errorMessage);

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
};
