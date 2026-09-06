#pragma once

#include <functional>

#include "AppSettings.h"
#include "DetachableWindow.h"
#include "SoundboardEngine.h"
#include "SoundboardGridComponent.h"
#include "SoundboardLayout.h"

// Hosts SoundboardGridComponent. Used to be permanently embedded, filling
// whatever width was left in the old single window; now its own
// hideable satellite, toggled from the Player window's "Soundboard..."
// button (and its own close button, which just hides it the same way).
class SoundboardWindow : public DetachableWindow
{
public:
    SoundboardWindow(AppSettings& settingsToUse, SoundboardEngine& soundboard,
                       SoundboardLayout& soundboardLayout, std::function<void()> onLayoutChanged);

    void closeButtonPressed() override;
};
