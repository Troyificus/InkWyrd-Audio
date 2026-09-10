#pragma once

#include <functional>

#include "AppSettings.h"
#include "DetachableWindow.h"
#include "MasterEngine.h"
#include "PlayerComponent.h"
#include "PlaylistEngine.h"

// Hosts PlayerComponent - transport, "now playing", and the switches that
// shape playback (crossfade/loop/fade-out/master volume). Closing this
// window quits the app, same as closing the old single MainWindow always
// did: this is the central window of the layout, not a satellite.
//
// It's also the MASTER window: the only one in the layout with a minimise
// button, and minimising it takes every open satellite down with it,
// bringing them all back together on restore. Having to minimise and
// restore five windows by hand was the actual complaint this solves.
class PlayerWindow : public DetachableWindow
{
public:
    PlayerWindow(AppSettings& settingsToUse,
                 PlaylistEngine& playlist,
                 MasterEngine& masterEngine,
                 std::function<void()> onTogglePlaylist,
                 std::function<void()> onToggleLibrary,
                 std::function<void()> onToggleVoiceFx,
                 std::function<void()> onToggleSoundboard,
                 std::function<void()> onSettingsClicked);

    PlayerComponent& getPlayerComponent() { return *playerComponent; }

    void closeButtonPressed() override;

    // The one hook that covers EVERY way a window gets minimised or
    // restored - the in-app button (DocumentWindow::minimiseButtonPressed
    // calls setMinimised, which routes through the peer), the taskbar,
    // Win+D, Aero shake. ComponentPeer::handleMovedOrResized calls this
    // whenever the peer's minimised state actually changes, so nothing
    // needs polling and no path is missed.
    void minimisationStateChanged(bool isNowMinimised) override;

    // The master window, and the only one that carries docked windows
    // along when dragged - satellites can therefore be pulled off the
    // group freely. See DetachableWindow::carriesDockedWindows().
    bool carriesDockedWindows() const override { return true; }

private:
    PlayerComponent* playerComponent = nullptr; // owned via setContentOwned

    // Which satellites this window hid on its way down, so restore brings
    // back exactly those and not, say, one the user had deliberately
    // closed beforehand. SafePointer because Settings can tear the layout
    // down while this window is minimised.
    juce::Array<juce::Component::SafePointer<DetachableWindow>> satellitesHiddenOnMinimise;
    bool satellitesAreHidden = false;
};
