#include "PlayerWindow.h"

#include "WindowLayoutStore.h"

namespace
{
    juce::Rectangle<int> defaultPlayerBounds()
    {
        auto area = WindowLayoutStore::primaryDisplayArea();
        return juce::Rectangle<int>(640, 320).withPosition(area.getX() + 40, area.getY() + 40);
    }
}

PlayerWindow::PlayerWindow(AppSettings& settingsToUse,
                            PlaylistEngine& playlist,
                            MasterEngine& masterEngine,
                            std::function<void()> onToggleVoiceFx,
                            std::function<void()> onToggleSoundboard,
                            std::function<void()> onSettingsClicked)
    : DetachableWindow("Inkwyrd Audio", "player", settingsToUse, defaultPlayerBounds())
{
    auto* component = new PlayerComponent(playlist, masterEngine,
                                           std::move(onToggleVoiceFx),
                                           std::move(onToggleSoundboard),
                                           std::move(onSettingsClicked));
    playerComponent = component;

    // false: DetachableWindow's base constructor already sized/positioned
    // this window (restored, or the matching default above) BEFORE this
    // line runs. Passing true here would immediately re-fit the window to
    // PlayerComponent's own fixed setSize(), silently discarding a
    // restored size on every launch - resized() (always called by
    // setContentOwned regardless of this flag) still stretches the
    // content to fill whatever the window's real size is.
    setContentOwned(component, false);
    setVisible(true);
}

void PlayerWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
