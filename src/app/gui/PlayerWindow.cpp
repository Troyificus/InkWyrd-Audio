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
    : DetachableWindow("Inkwyrd Audio", "player", settingsToUse, defaultPlayerBounds(), true,
                       DocumentWindow::closeButton | DocumentWindow::minimizeButton)
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

void PlayerWindow::setMinimised(bool shouldBeMinimised)
{
    if (shouldBeMinimised != isMinimised())
    {
        if (shouldBeMinimised)
        {
            satellitesVisibleBeforeMinimize.clear();
            for (auto* win : getActiveWindows())
            {
                if (win != this && win->isVisible())
                {
                    satellitesVisibleBeforeMinimize.add(win);
                    win->setVisible(false);
                }
            }
        }
        else
        {
            for (auto win : satellitesVisibleBeforeMinimize)
            {
                if (win != nullptr)
                    win->setVisible(true);
            }
            satellitesVisibleBeforeMinimize.clear();
        }
    }

    DocumentWindow::setMinimised(shouldBeMinimised);
}

void PlayerWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
