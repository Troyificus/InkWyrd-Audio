#include "VoiceFxWindow.h"

#include "WindowLayoutStore.h"

namespace
{
    juce::Rectangle<int> defaultVoiceFxBounds()
    {
        auto area = WindowLayoutStore::primaryDisplayArea();
        return juce::Rectangle<int>(820, 520).withPosition(area.getX() + 40, area.getY() + 40);
    }
}

VoiceFxWindow::VoiceFxWindow(AppSettings& settingsToUse, PluginScanner& scanner, PluginChain& voiceChain,
                              std::function<void()> onPluginListChanged)
    // defaultVisible = false: a hideable satellite should open on a
    // clean first launch, not clutter the screen unasked.
    : DetachableWindow("Voice FX", "voiceFx", settingsToUse, defaultVoiceFxBounds(), false)
{
    // VoiceFxComponent sets its own size (820x520) in its constructor,
    // matching defaultVoiceFxBounds() above. false - see PlayerWindow.cpp
    // for why: the window's own (restored or default) bounds must not be
    // overridden by the content's fixed size.
    setContentOwned(new VoiceFxComponent(scanner, voiceChain, std::move(onPluginListChanged)), false);
    setVisible(wasVisibleWhenSaved());
}

void VoiceFxWindow::closeButtonPressed()
{
    setVisible(false);
}
