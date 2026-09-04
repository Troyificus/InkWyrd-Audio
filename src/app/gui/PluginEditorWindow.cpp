#include "PluginEditorWindow.h"

PluginEditorWindow::PluginEditorWindow(juce::AudioPluginInstance& pluginToUse,
                                        std::function<void(PluginEditorWindow*)> onCloseToUse)
    : juce::DocumentWindow(pluginToUse.getName(),
                            juce::Desktop::getInstance().getDefaultLookAndFeel()
                                .findColour(juce::ResizableWindow::backgroundColourId),
                            juce::DocumentWindow::closeButton),
      plugin(pluginToUse),
      onClose(std::move(onCloseToUse))
{
    // createEditorIfNeeded returns null for a plugin with no interface of
    // its own, which is common for utility plugins - the generic editor
    // keeps those adjustable rather than leaving an empty window.
    auto* editor = plugin.hasEditor() ? plugin.createEditorIfNeeded()
                                      : new juce::GenericAudioProcessorEditor(plugin);

    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor(plugin);

    setUsingNativeTitleBar(true);
    setContentOwned(editor, true);
    setResizable(editor->isResizable(), false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

PluginEditorWindow::~PluginEditorWindow()
{
    // Releases the editor back to the plugin (AudioProcessor::
    // editorBeingDeleted) before this window goes away.
    clearContentComponent();
}

void PluginEditorWindow::closeButtonPressed()
{
    if (onClose)
        onClose(this); // the owner deletes us
}
