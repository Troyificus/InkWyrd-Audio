#include "PluginChain.h"

PluginChain::PluginChain()
    : juce::AudioProcessor(BusesProperties()
                                .withInput("Input", juce::AudioChannelSet::stereo())
                                .withOutput("Output", juce::AudioChannelSet::stereo()))
{
}

bool PluginChain::addPlugin(PluginScanner& scanner, const juce::PluginDescription& description, juce::String& errorMessage)
{
    auto instance = scanner.createInstance(description, currentSampleRate, currentBlockSize, errorMessage);
    if (instance == nullptr)
        return false;

    instance->prepareToPlay(currentSampleRate, currentBlockSize);

    const juce::ScopedLock sl(chainLock);
    plugins.add(instance.release());
    return true;
}

void PluginChain::removePlugin(int index)
{
    const juce::ScopedLock sl(chainLock);
    if (auto* plugin = plugins[index])
        plugin->releaseResources();
    plugins.remove(index);
}

int PluginChain::getNumPlugins() const
{
    const juce::ScopedLock sl(chainLock);
    return plugins.size();
}

juce::String PluginChain::getPluginName(int index) const
{
    const juce::ScopedLock sl(chainLock);
    if (auto* plugin = plugins[index])
        return plugin->getName();
    return {};
}

void PluginChain::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    const juce::ScopedLock sl(chainLock);
    for (auto* plugin : plugins)
        plugin->prepareToPlay(sampleRate, samplesPerBlock);
}

void PluginChain::releaseResources()
{
    const juce::ScopedLock sl(chainLock);
    for (auto* plugin : plugins)
        plugin->releaseResources();
}

void PluginChain::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    const juce::ScopedLock sl(chainLock);
    for (auto* plugin : plugins)
    {
        plugin->processBlock(buffer, midiMessages);
        // Don't let one plugin's MIDI output (if any) leak into the
        // next plugin's input - we're not routing MIDI through this
        // chain, only audio.
        midiMessages.clear();
    }
}
