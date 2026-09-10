#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "NoiseSuppressor.h"
#include "PluginChain.h"
#include "PluginEditorWindow.h"
#include "PluginScanner.h"

// The VST3 plugins the user has chosen, and the live mic chain built
// from them.
//
// Two deliberate changes from the first version:
//
// 1. It no longer lists every VST3 installed on the machine. That was a
//    scan of the whole system folder producing dozens of plugins, almost
//    none of which anyone wants on a microphone, and it cost 15-20
//    seconds on first launch. Now there is an "Add VST3..." button that
//    opens at the system plugin folder, and the list holds only what the
//    user picked. It persists between sessions.
//
// 2. Adding a plugin opens the plugin's OWN interface, and every plugin
//    in the chain has an Edit button to reopen it. Loading something
//    like an EQ at its defaults with no way to touch it - which is what
//    happened before - is barely worth having.
class VoiceFxComponent : public juce::Component
{
public:
    VoiceFxComponent(PluginScanner& scannerToUse,
                      PluginChain& voiceChainToUse,
                      NoiseSuppressor& noiseSuppressorToUse,
                      bool noiseSuppressionEnabled,
                      std::function<void()> onPluginListChangedToUse,
                      std::function<void(bool)> onNoiseSuppressionChangedToUse);

    // Editor windows are closed here, before anything they point into can
    // go away.
    ~VoiceFxComponent() override;

    void resized() override;

private:
    void rebuildPluginListUI();
    void rebuildChainListUI();
    void updateNoiseSuppressionHint();

    void browseForPlugin();
    void addToChain(const juce::PluginDescription& description);
    void removeFromList(const juce::PluginDescription& description);

    void openEditorFor(int chainIndex);
    void closeEditorFor(const juce::AudioPluginInstance* plugin);
    void closeAllEditors();

    PluginScanner& scanner;
    PluginChain& voiceChain;
    NoiseSuppressor& noiseSuppressor;
    std::function<void()> onPluginListChanged;
    std::function<void(bool)> onNoiseSuppressionChanged;

    // Above the two plugin columns, because it applies to the whole mic
    // path rather than being one more item in the chain - and because it
    // runs BEFORE the chain does, so listing it among the plugins would
    // misrepresent the signal order.
    juce::ToggleButton noiseSuppressionToggle { "Noise suppression (RNNoise)" };
    juce::Label noiseSuppressionHint;

    juce::Label pluginListCaption { {}, "Your VST3 plugins" };
    juce::TextButton addPluginButton { "Add VST3..." };
    juce::Label emptyMessage;
    juce::Viewport pluginListViewport;
    juce::Component pluginListPanel;
    juce::OwnedArray<juce::TextButton> addToChainButtons;
    juce::OwnedArray<juce::TextButton> forgetButtons;

    juce::Label chainListCaption { {}, "Live voice chain" };
    juce::Label chainHint;
    juce::Viewport chainListViewport;
    juce::Component chainListPanel;
    juce::OwnedArray<juce::TextButton> editChainButtons;
    juce::OwnedArray<juce::TextButton> removeChainButtons;

    juce::OwnedArray<PluginEditorWindow> editorWindows;

    std::unique_ptr<juce::FileChooser> activeChooser;
};
