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

    // Alphabetical, so the Voice FX list is in a findable order rather
    // than whatever order the filesystem happened to yield - and so a
    // cached list and a freshly scanned one come out identical rather
    // than merely equivalent.
    knownPlugins.sort(juce::KnownPluginList::sortAlphabetically, true);
    return knownPlugins.getTypes();
}

void PluginScanner::restoreFromCache(const juce::File& cacheFile)
{
    if (!cacheFile.existsAsFile())
        return;

    if (auto xml = juce::parseXML(cacheFile))
    {
        knownPlugins.recreateFromXml(*xml);
        knownPlugins.sort(juce::KnownPluginList::sortAlphabetically, true);
    }
}

void PluginScanner::saveToCache(const juce::File& cacheFile) const
{
    if (auto xml = knownPlugins.createXml())
    {
        cacheFile.getParentDirectory().createDirectory();

        // Atomic, same reasoning as the playlist and soundboard files: a
        // half-written cache would be parsed as a short plugin list, and
        // the user would silently lose plugins from the Voice FX panel
        // with no indication why.
        juce::TemporaryFile temp(cacheFile);
        if (temp.getFile().replaceWithText(xml->toString()))
            temp.overwriteTargetFileWithTemporary();
    }
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
