#include "PlayerWindow.h"

#include "WindowLayoutStore.h"

namespace
{
    juce::Rectangle<int> defaultPlayerBounds()
    {
        auto area = WindowLayoutStore::primaryDisplayArea();
        return juce::Rectangle<int>(640, 596).withPosition(area.getX() + 40, area.getY() + 40);
    }
}

PlayerWindow::PlayerWindow(AppSettings& settingsToUse,
                            PlaylistEngine& playlist,
                            MasterEngine& masterEngine,
                            const TrackMetadataStore& trackMetadata,
                            std::function<void()> onTogglePlaylist,
                            std::function<void()> onToggleLibrary,
                            std::function<void()> onToggleVoiceFx,
                            std::function<void()> onToggleSoundboard,
                            std::function<void()> onSettingsClicked)
    // The master window is the only one that gets a minimise button.
    : DetachableWindow("Inkwyrd Audio", "player", "Audio Player", settingsToUse, defaultPlayerBounds(), true,
                        juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
{
    auto* component = new PlayerComponent(playlist, masterEngine, trackMetadata,
                                           std::move(onTogglePlaylist),
                                           std::move(onToggleLibrary),
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

    // Anyone upgrading has a saved height from before the now-playing
    // display existed, and restoring it verbatim leaves the transport
    // squeezed off the bottom of a window they never chose to make that
    // small. Grow it once, keeping their position and width - a saved
    // layout IS a preference, but it was expressed about a different
    // window.
    constexpr int kMinimumUsefulHeight = 596;
    if (getHeight() < kMinimumUsefulHeight)
        setSize(getWidth(), kMinimumUsefulHeight);

    setVisible(true);
}

void PlayerWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void PlayerWindow::minimisationStateChanged(bool isNowMinimised)
{
    DetachableWindow::minimisationStateChanged(isNowMinimised);

    // Idempotent: this can be called for reasons other than a genuine
    // state flip, and hiding an already-hidden set would lose track of
    // which satellites to bring back.
    if (isNowMinimised == satellitesAreHidden)
        return;

    if (isNowMinimised)
    {
        satellitesHiddenOnMinimise.clear();

        for (auto* window : getActiveWindows())
        {
            if (window == this || ! window->isVisible())
                continue;

            satellitesHiddenOnMinimise.add(window);
            window->setHiddenByMasterMinimise(true);
        }
    }
    else
    {
        for (auto& member : satellitesHiddenOnMinimise)
            if (auto* window = member.getComponent())
                window->setHiddenByMasterMinimise(false);

        satellitesHiddenOnMinimise.clear();
    }

    satellitesAreHidden = isNowMinimised;
}
