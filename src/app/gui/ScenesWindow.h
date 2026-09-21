#pragma once

#include "AppSettings.h"
#include "DetachableWindow.h"
#include "SceneLibrary.h"
#include "ScenesComponent.h"

// Hosts ScenesComponent - see its header for what the window is for and
// why it is a window of its own. Split from the component so the
// self-test can build and draw the component without any of the window
// machinery.
class ScenesWindow : public DetachableWindow
{
public:
    ScenesWindow(AppSettings& settingsToUse, SceneLibrary& library, ScenesComponent::Callbacks callbacks);

    ScenesComponent& getScenes() { return *scenes; }

private:
    ScenesComponent* scenes = nullptr; // owned via setContentOwned
};
