#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AppSettings.h"
#include "DetachableWindow.h"
#include "SetupComponent.h"

// The Setup/Settings window. Used to also swap in and host PlayerComponent
// directly - that moved out into its own PlayerWindow once the Winamp-
// style layout split the single main window into five - so this now only
// ever holds a SetupComponent, shown on first run and whenever the user
// clicks Settings from the Player window.
//
// Also the first DetachableWindow in the app, from when this class WAS
// the only window - proved bounds persist/restore/clamp correctly before
// that base got multiplied out into five window classes.
class MainWindow : public DetachableWindow
{
public:
    MainWindow(const juce::String& name, AppSettings& settingsToUse);

    void showSetupView(AppSettings& settings, bool isFirstRun,
                        std::function<void(SetupComponent::Result)> onSaveAndLaunch,
                        std::function<void(juce::String, SetupComponent::AuthoriseCallback)> onAuthoriseRpc);

    void closeButtonPressed() override;
};
