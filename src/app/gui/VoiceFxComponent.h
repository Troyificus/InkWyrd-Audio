#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginChain.h"
#include "PluginScanner.h"

// The VST3 plugin list and the live mic chain, lifted out of
// PlayerComponent unchanged so the main screen can be playlist + SFX.
//
// This is a straight move, not a redesign: the user asked for voice
// effects to live on the main screen "somewhere" but explicitly deferred
// designing that, so it lives behind a Voice FX... button for now rather
// than being squeezed into a third column or dropped.
class VoiceFxComponent : public juce::Component
{
public:
    VoiceFxComponent(PluginScanner& scannerToUse,
                      PluginChain& voiceChainToUse,
                      juce::Array<juce::PluginDescription> availablePluginsToUse);

    void resized() override;

private:
    void rebuildChainListUI();

    PluginScanner& scanner;
    PluginChain& voiceChain;
    juce::Array<juce::PluginDescription> availablePlugins;

    juce::Label pluginListCaption { {}, "Available VST3 plugins" };
    juce::Viewport pluginListViewport;
    juce::Component pluginListPanel;
    juce::OwnedArray<juce::TextButton> addPluginButtons;

    juce::Label chainListCaption { {}, "Live voice chain" };
    juce::Viewport chainListViewport;
    juce::Component chainListPanel;
    juce::OwnedArray<juce::TextButton> removeChainButtons;
};
