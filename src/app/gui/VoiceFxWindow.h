#pragma once

#include <functional>

#include "AppSettings.h"
#include "DetachableWindow.h"
#include "PluginChain.h"
#include "PluginScanner.h"
#include "VoiceFxComponent.h"

// Hosts VoiceFxComponent. Used to be launched fresh into a throwaway
// juce::DialogWindow every time and destroyed on close - that meant its
// own open PluginEditorWindows (one per plugin in the chain) were also
// destroyed and rebuilt from scratch on every reopen. Now constructed
// ONCE and kept alive for the app's lifetime; the Player window's
// "Voice FX..." button and this window's own close button both just
// toggle visibility, so a hide/show cycle keeps everything intact.
class VoiceFxWindow : public DetachableWindow
{
public:
    VoiceFxWindow(AppSettings& settingsToUse, PluginScanner& scanner, PluginChain& voiceChain,
                   std::function<void()> onPluginListChanged);

    void closeButtonPressed() override;
};
