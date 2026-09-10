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
                              NoiseSuppressor& noiseSuppressor,
                              std::function<void()> onPluginListChanged)
    // defaultVisible = false: a hideable satellite should open on a
    // clean first launch, not clutter the screen unasked.
    : DetachableWindow("Voice FX", "voiceFx", "Voice FX", settingsToUse, defaultVoiceFxBounds(), false)
{
    // VoiceFxComponent sets its own size (820x520) in its constructor,
    // matching defaultVoiceFxBounds() above. false - see PlayerWindow.cpp
    // for why: the window's own (restored or default) bounds must not be
    // overridden by the content's fixed size.
    // The engine is the source of truth for whether suppression is
    // running; settings only says what it should be restored to. Applied
    // here rather than in the component so the setting takes effect even
    // if this window is never opened.
    noiseSuppressor.setEnabled(settingsToUse.isNoiseSuppressionEnabled());

    setContentOwned(new VoiceFxComponent(scanner, voiceChain, noiseSuppressor,
                                          settingsToUse.isNoiseSuppressionEnabled(),
                                          std::move(onPluginListChanged),
                                          [&settingsToUse](bool enabled)
                                          {
                                              settingsToUse.setNoiseSuppressionEnabled(enabled);
                                              settingsToUse.save();
                                          }),
                     false);
    setVisible(wasVisibleWhenSaved());
}
