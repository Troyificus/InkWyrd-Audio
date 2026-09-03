#include "VoiceFxComponent.h"

namespace
{
    constexpr int kRowHeight = 28;
    constexpr int kRowSpacing = 4;

    void layoutRows(juce::Component& panel, juce::OwnedArray<juce::TextButton>& buttons, int width)
    {
        width = juce::jmax(0, width);
        int y = 0;
        for (auto* button : buttons)
        {
            button->setBounds(0, y, width, kRowHeight);
            y += kRowHeight + kRowSpacing;
        }
        panel.setSize(width, juce::jmax(kRowHeight, y));
    }
}

VoiceFxComponent::VoiceFxComponent(PluginScanner& scannerToUse,
                                    PluginChain& voiceChainToUse,
                                    juce::Array<juce::PluginDescription> availablePluginsToUse)
    : scanner(scannerToUse), voiceChain(voiceChainToUse), availablePlugins(std::move(availablePluginsToUse))
{
    addAndMakeVisible(pluginListCaption);
    pluginListViewport.setViewedComponent(&pluginListPanel, false);
    addAndMakeVisible(pluginListViewport);

    for (int i = 0; i < availablePlugins.size(); ++i)
    {
        auto description = availablePlugins.getReference(i);
        auto* button = addPluginButtons.add(new juce::TextButton("Add: " + description.name));
        pluginListPanel.addAndMakeVisible(button);
        button->onClick = [this, description]
        {
            juce::String error;
            if (!voiceChain.addPlugin(scanner, description, error))
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Couldn't add plugin", error);
            rebuildChainListUI();
        };
    }

    addAndMakeVisible(chainListCaption);
    chainListViewport.setViewedComponent(&chainListPanel, false);
    addAndMakeVisible(chainListViewport);
    rebuildChainListUI();

    setSize(760, 520);
}

void VoiceFxComponent::rebuildChainListUI()
{
    removeChainButtons.clear();

    auto n = voiceChain.getNumPlugins();
    for (int i = 0; i < n; ++i)
    {
        auto* button = removeChainButtons.add(new juce::TextButton("Remove: " + voiceChain.getPluginName(i)));
        chainListPanel.addAndMakeVisible(button);
        button->onClick = [this, i]
        {
            if (i < voiceChain.getNumPlugins())
                voiceChain.removePlugin(i);
            rebuildChainListUI();
        };
    }

    layoutRows(chainListPanel, removeChainButtons, chainListViewport.getWidth() - chainListViewport.getScrollBarThickness());
}

void VoiceFxComponent::resized()
{
    auto area = getLocalBounds().reduced(16);
    auto columnWidth = (area.getWidth() - 12) / 2;

    auto pluginColumn = area.removeFromLeft(columnWidth);
    area.removeFromLeft(12);
    auto chainColumn = area;

    pluginListCaption.setBounds(pluginColumn.removeFromTop(22));
    pluginListViewport.setBounds(pluginColumn);
    layoutRows(pluginListPanel, addPluginButtons, pluginListViewport.getWidth() - pluginListViewport.getScrollBarThickness());

    chainListCaption.setBounds(chainColumn.removeFromTop(22));
    chainListViewport.setBounds(chainColumn);
    layoutRows(chainListPanel, removeChainButtons, chainListViewport.getWidth() - chainListViewport.getScrollBarThickness());
}
