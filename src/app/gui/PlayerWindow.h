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
class PlayerWindow : public DetachableWindow
{
public:
    PlayerWindow(AppSettings& settingsToUse,
                 PlaylistEngine& playlist,
                 MasterEngine& masterEngine,
                 std::function<void()> onToggleVoiceFx,
                 std::function<void()> onToggleSoundboard,
                 std::function<void()> onSettingsClicked);

    PlayerComponent& getPlayerComponent() { return *playerComponent; }

    void closeButtonPressed() override;

private:
    PlayerComponent* playerComponent = nullptr; // owned via setContentOwned
};
