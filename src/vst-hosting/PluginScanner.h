#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Holds the set of VST3 plugins the user has chosen for the voice chain,
// and knows how to instantiate one from its description. VST3 only, per
// the design brief - no VST2 support.
//
// The app deliberately does NOT sweep the whole VST3 folder any more.
// A full scan produced a list of everything installed - dozens of
// plugins on a working machine, almost none of which anyone wants on a
// microphone - and cost 15-20 seconds the first time. The user picks
// specific .vst3 files instead; addPluginsFromFile() reads just those.
//
// scan() is kept for the standalone VstHostingTest harness, which still
// wants "what is on this machine", and for the cache round-trip test.
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

    // Reads the plugin types out of ONE .vst3 and adds them to the known
    // set. A single .vst3 can legitimately contain several plugins, so
    // this returns how many it found. 0 with errorMessage filled in
    // means the file wasn't a VST3 this host could read.
    int addPluginsFromFile(const juce::File& file, juce::String& errorMessage);

    void removePlugin(const juce::PluginDescription& description);

    // Where the file chooser should open: the standard system VST3
    // folder, so picking one is a couple of clicks rather than a hunt.
    static juce::File getDefaultPluginFolder();

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
