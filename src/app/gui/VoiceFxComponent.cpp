#include "VoiceFxComponent.h"

#include "Dialogs.h"

namespace
{
    constexpr int kRowHeight = 28;
    constexpr int kRowSpacing = 4;
    constexpr int kForgetButtonWidth = 74;
    constexpr int kEditButtonWidth = 60;

    // One row = the main button plus an optional narrower one on its
    // right, so "add this plugin" and "forget this plugin" (or "edit"
    // and "remove") sit on one line each.
    void layoutRows(juce::Component& panel,
                     juce::OwnedArray<juce::TextButton>& mainButtons,
                     juce::OwnedArray<juce::TextButton>& trailingButtons,
                     int trailingWidth,
                     int width)
    {
        width = juce::jmax(0, width);
        int y = 0;

        for (int i = 0; i < mainButtons.size(); ++i)
        {
            juce::Rectangle<int> row(0, y, width, kRowHeight);

            if (auto* trailing = trailingButtons[i])
            {
                trailing->setBounds(row.removeFromRight(juce::jmin(trailingWidth, width)));
                row.removeFromRight(4);
            }

            mainButtons[i]->setBounds(row);
            y += kRowHeight + kRowSpacing;
        }

        panel.setSize(width, juce::jmax(kRowHeight, y));
    }
}

VoiceFxComponent::VoiceFxComponent(PluginScanner& scannerToUse,
                                    PluginChain& voiceChainToUse,
                                    std::function<void()> onPluginListChangedToUse)
    : scanner(scannerToUse),
      voiceChain(voiceChainToUse),
      onPluginListChanged(std::move(onPluginListChangedToUse))
{
    addAndMakeVisible(pluginListCaption);

    addPluginButton.onClick = [this] { browseForPlugin(); };
    addAndMakeVisible(addPluginButton);

    emptyMessage.setText("No plugins added yet. Click \"Add VST3...\" and pick the ones you want "
                          "for your microphone.", juce::dontSendNotification);
    emptyMessage.setColour(juce::Label::textColourId, juce::Colours::grey);
    emptyMessage.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(emptyMessage);

    pluginListViewport.setViewedComponent(&pluginListPanel, false);
    addAndMakeVisible(pluginListViewport);

    addAndMakeVisible(chainListCaption);

    chainHint.setText("These run on your microphone, in order. Edit opens the plugin's own window.",
                       juce::dontSendNotification);
    chainHint.setColour(juce::Label::textColourId, juce::Colours::grey);
    chainHint.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(chainHint);

    chainListViewport.setViewedComponent(&chainListPanel, false);
    addAndMakeVisible(chainListViewport);

    rebuildPluginListUI();
    rebuildChainListUI();

    setSize(820, 520);
}

VoiceFxComponent::~VoiceFxComponent()
{
    // Before anything these point into can go away.
    closeAllEditors();
}

void VoiceFxComponent::rebuildPluginListUI()
{
    addToChainButtons.clear();
    forgetButtons.clear();

    auto plugins = scanner.getKnownPlugins();

    for (const auto& description : plugins)
    {
        auto* add = addToChainButtons.add(new juce::TextButton(description.name));
        add->setTooltip(description.manufacturerName + " - " + description.fileOrIdentifier);
        pluginListPanel.addAndMakeVisible(add);
        add->onClick = [this, description] { addToChain(description); };

        auto* forget = forgetButtons.add(new juce::TextButton("Forget"));
        forget->setTooltip("Take this plugin off the list. Anything already in the chain stays.");
        pluginListPanel.addAndMakeVisible(forget);
        forget->onClick = [this, description] { removeFromList(description); };
    }

    auto empty = plugins.isEmpty();
    emptyMessage.setVisible(empty);
    pluginListViewport.setVisible(!empty);

    resized();
}

void VoiceFxComponent::rebuildChainListUI()
{
    editChainButtons.clear();
    removeChainButtons.clear();

    auto n = voiceChain.getNumPlugins();
    for (int i = 0; i < n; ++i)
    {
        auto* edit = editChainButtons.add(new juce::TextButton(voiceChain.getPluginName(i)));
        chainListPanel.addAndMakeVisible(edit);
        edit->onClick = [this, i] { openEditorFor(i); };

        auto* remove = removeChainButtons.add(new juce::TextButton("Remove"));
        chainListPanel.addAndMakeVisible(remove);
        remove->onClick = [this, i]
        {
            if (i >= voiceChain.getNumPlugins())
                return;

            // The editor holds a reference into the instance, so it has
            // to go first - removing the plugin underneath an open window
            // would leave that window pointing at freed memory.
            closeEditorFor(voiceChain.getPlugin(i));
            voiceChain.removePlugin(i);
            rebuildChainListUI();
        };
    }

    chainHint.setVisible(n > 0);

    layoutRows(chainListPanel, editChainButtons, removeChainButtons, kEditButtonWidth,
                chainListViewport.getWidth() - chainListViewport.getScrollBarThickness());
}

void VoiceFxComponent::browseForPlugin()
{
    auto startIn = PluginScanner::getDefaultPluginFolder();

    activeChooser = std::make_unique<juce::FileChooser>("Choose a VST3 plugin", startIn, "*.vst3");
    activeChooser->launchAsync(juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::canSelectDirectories
                                   | juce::FileBrowserComponent::canSelectMultipleItems,
                                [this, safeThis = juce::Component::SafePointer<VoiceFxComponent>(this)]
                                (const juce::FileChooser& chooser)
    {
        auto results = chooser.getResults();
        if (results.isEmpty() || safeThis == nullptr)
            return;

        int added = 0;
        juce::StringArray problems;

        for (const auto& file : results)
        {
            juce::String error;
            auto count = scanner.addPluginsFromFile(file, error);
            added += count;

            if (count == 0 && error.isNotEmpty())
                problems.add(error);
        }

        if (added > 0)
        {
            rebuildPluginListUI();

            if (onPluginListChanged)
                onPluginListChanged();
        }

        if (!problems.isEmpty())
            inkwyrd::showMessage(safeThis, added > 0 ? juce::MessageBoxIconType::InfoIcon
                                                      : juce::MessageBoxIconType::WarningIcon,
                                  added > 0 ? "Some weren't added" : "Couldn't add that",
                                  problems.joinIntoString("\n"));
    });
}

void VoiceFxComponent::addToChain(const juce::PluginDescription& description)
{
    juce::String error;
    if (!voiceChain.addPlugin(scanner, description, error))
    {
        inkwyrd::showMessage(this, juce::MessageBoxIconType::WarningIcon,
                              "Couldn't add plugin", error);
        return;
    }

    rebuildChainListUI();

    // Straight into the plugin's own window. Adding something like an EQ
    // and leaving it at its defaults, with no obvious way to change it,
    // is the thing this whole panel was rebuilt to fix.
    openEditorFor(voiceChain.getNumPlugins() - 1);
}

void VoiceFxComponent::removeFromList(const juce::PluginDescription& description)
{
    // Only the list, never the chain: taking a plugin off the shelf
    // shouldn't silently pull it out of a mic chain mid-session.
    scanner.removePlugin(description);
    rebuildPluginListUI();

    if (onPluginListChanged)
        onPluginListChanged();
}

void VoiceFxComponent::openEditorFor(int chainIndex)
{
    auto* plugin = voiceChain.getPlugin(chainIndex);
    if (plugin == nullptr)
        return;

    // Already open - bring it forward rather than stacking a second copy.
    for (auto* window : editorWindows)
    {
        if (window->getPlugin() == plugin)
        {
            window->toFront(true);
            return;
        }
    }

    editorWindows.add(new PluginEditorWindow(*plugin, [this](PluginEditorWindow* window)
    {
        editorWindows.removeObject(window);
    }));
}

void VoiceFxComponent::closeEditorFor(const juce::AudioPluginInstance* plugin)
{
    for (int i = editorWindows.size(); --i >= 0;)
        if (editorWindows[i]->getPlugin() == plugin)
            editorWindows.remove(i);
}

void VoiceFxComponent::closeAllEditors()
{
    editorWindows.clear();
}

void VoiceFxComponent::resized()
{
    auto area = getLocalBounds().reduced(16);
    auto columnWidth = (area.getWidth() - 12) / 2;

    auto pluginColumn = area.removeFromLeft(columnWidth);
    area.removeFromLeft(12);
    auto chainColumn = area;

    auto captionRow = pluginColumn.removeFromTop(26);
    addPluginButton.setBounds(captionRow.removeFromRight(110).reduced(0, 1));
    pluginListCaption.setBounds(captionRow);
    pluginColumn.removeFromTop(6);

    if (emptyMessage.isVisible())
    {
        emptyMessage.setBounds(pluginColumn.removeFromTop(56));
    }
    else
    {
        pluginListViewport.setBounds(pluginColumn);
        layoutRows(pluginListPanel, addToChainButtons, forgetButtons, kForgetButtonWidth,
                    pluginListViewport.getWidth() - pluginListViewport.getScrollBarThickness());
    }

    chainListCaption.setBounds(chainColumn.removeFromTop(26));

    if (chainHint.isVisible())
    {
        chainHint.setBounds(chainColumn.removeFromTop(18));
        chainColumn.removeFromTop(4);
    }

    chainListViewport.setBounds(chainColumn);
    layoutRows(chainListPanel, editChainButtons, removeChainButtons, kEditButtonWidth,
                chainListViewport.getWidth() - chainListViewport.getScrollBarThickness());
}
