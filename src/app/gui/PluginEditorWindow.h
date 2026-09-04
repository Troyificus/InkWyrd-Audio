#pragma once

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// A window hosting a plugin's OWN interface - its knobs, its preset
// browser, whatever the vendor built - rather than a list of parameter
// names.
//
// Adding a plugin used to load it at its defaults with no way to change
// anything, which for something like an EQ or a de-esser makes it close
// to useless.
//
// A plugin that ships no interface of its own gets JUCE's generic
// parameter list instead, so every plugin is at least adjustable.
//
// Lifetime matters here: the editor belongs to the plugin instance, so
// this window MUST be closed before that instance is removed from the
// chain. VoiceFxComponent owns these and closes them first.
class PluginEditorWindow final : public juce::DocumentWindow
{
public:
    PluginEditorWindow(juce::AudioPluginInstance& pluginToUse,
                        std::function<void(PluginEditorWindow*)> onCloseToUse);

    ~PluginEditorWindow() override;

    void closeButtonPressed() override;

    const juce::AudioPluginInstance* getPlugin() const { return &plugin; }

private:
    juce::AudioPluginInstance& plugin;
    std::function<void(PluginEditorWindow*)> onClose;
};
