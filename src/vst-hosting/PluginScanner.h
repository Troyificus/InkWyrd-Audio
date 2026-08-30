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
    juce::Array<juce::PluginDescription> scan(const juce::File& extraFolder = {});

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
