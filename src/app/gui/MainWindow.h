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

    // Keeps Settings above Inkwyrd's OWN windows while it is open,
    // without pinning it above every other program on the desktop.
    //
    // setAlwaysOnTop is an operating-system topmost flag: it floats the
    // window over everything, so Settings sat on top of the user's
    // browser, their game and their Discord client until they closed it.
    // Annoying very quickly, and not what "above the rest of the app"
    // was meant to mean.
    void setFloatAboveApp(bool shouldFloat);

    void showSetupView(AppSettings& settings, bool isFirstRun,
                        std::function<void(SetupComponent::Result)> onSaveAndLaunch,
                        std::function<void(juce::String, SetupComponent::AuthoriseCallback)> onAuthoriseRpc,
                        SetupComponent::ApplySkinCallback onApplySkin = {});

    void closeButtonPressed() override;

    // What the title bar's X does. Left unset it quits the app, which is
    // what it has to mean on first run, when this window IS the app.
    std::function<void()> onCloseRequested;

private:
    // Polled rather than driven by activeWindowStatusChanged, which
    // reports on whichever top-level window was active: if the Player
    // window had focus at the moment the user switched to another
    // program, this window is never told and stays pinned over their
    // desktop. Process::isForegroundProcess asks the question that
    // actually matters - is any part of Inkwyrd in front - and four
    // times a second costs nothing while one window is open.
    //
    // A TimedCallback rather than inheriting juce::Timer: DetachableWindow
    // already does, privately, for its debounced bounds saving, and a
    // second Timer base would be ambiguous as well as hijacking that one.
    void syncFloatWithForeground();
    juce::TimedCallback floatCheck { [this] { syncFloatWithForeground(); } };
};
