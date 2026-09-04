#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginScanner.h"

// Hosts an ordered chain of loaded VST3 plugin instances and runs audio
// through them in sequence (in place). Derives from juce::AudioProcessor
// so it can plug directly into a juce::AudioProcessorPlayer for live
// mic-in -> chain -> speakers-out monitoring - the same machinery JUCE's
// own AudioPluginHost example uses, per the design brief.
class PluginChain : public juce::AudioProcessor
{
public:
    PluginChain();

    // Loads and appends a plugin to the end of the chain. Returns false
    // (with errorMessage filled in) if it failed to load - one bad
    // plugin shouldn't take the rest of the chain down.
    bool addPlugin(PluginScanner& scanner, const juce::PluginDescription& description, juce::String& errorMessage);
    void removePlugin(int index);
    int getNumPlugins() const;
    juce::String getPluginName(int index) const;

    // The live instance, so its own editor window can be opened.
    //
    // MESSAGE THREAD ONLY, and the pointer is only valid until the
    // plugin is removed - so anything holding on to it (an open editor
    // window) has to be closed BEFORE removePlugin() is called, not
    // after.
    juce::AudioPluginInstance* getPlugin(int index) const;

    // juce::AudioProcessor
    const juce::String getName() const override { return "Inkwyrd VST3 Chain"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    // addPlugin/removePlugin are expected to be called from the message
    // thread (typically in response to user action) while processBlock
    // runs concurrently on the audio thread - this guards the small
    // window where the chain's structure actually changes. Not a
    // hot-path lock: processBlock holds it for the whole chain's
    // processing, but plugin add/remove is a rare, user-triggered event,
    // not something happening every block.
    juce::CriticalSection chainLock;
    juce::OwnedArray<juce::AudioPluginInstance> plugins;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
};
