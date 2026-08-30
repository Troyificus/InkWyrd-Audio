#include "PluginScanner.h"

PluginScanner::PluginScanner()
{
    formatManager.addFormat(new juce::VST3PluginFormat());
}

juce::Array<juce::PluginDescription> PluginScanner::scan(const juce::File& extraFolder)
{
    auto* vst3Format = formatManager.getFormat(0);

    auto searchPath = vst3Format->getDefaultLocationsToSearch();
    if (extraFolder != juce::File())
        searchPath.add(extraFolder);

    // A "dead man's pedal" file: the scanner writes to it before probing
    // each plugin and clears it after, so a plugin that crashes the
    // scanner process leaves it behind and gets skipped on the next
    // run instead of crashing every future scan too.
    auto deadMansPedal = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("InkwyrdAudio_vst3_deadmanspedal.txt");

    juce::PluginDirectoryScanner scanner(knownPlugins, *vst3Format, searchPath, true, deadMansPedal);

    juce::String pluginBeingScanned;
    while (scanner.scanNextFile(true, pluginBeingScanned))
    {
        // PluginDirectoryScanner scans one file per call - nothing to
        // do with progress here since this is a headless scan, but the
        // loop has to keep pumping it to actually scan everything.
    }

    return knownPlugins.getTypes();
}

juce::Array<juce::PluginDescription> PluginScanner::getKnownPlugins() const
{
    return knownPlugins.getTypes();
}

std::unique_ptr<juce::AudioPluginInstance> PluginScanner::createInstance(const juce::PluginDescription& description,
                                                                           double sampleRate,
                                                                           int blockSize,
                                                                           juce::String& errorMessage)
{
    return formatManager.createPluginInstance(description, sampleRate, blockSize, errorMessage);
}
