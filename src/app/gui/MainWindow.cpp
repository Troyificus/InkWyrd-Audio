#include "MainWindow.h"

#include "WindowLayoutStore.h"

namespace
{
    // What centreWithSize(900, 620) used to produce - kept as the
    // fallback default for a first launch, before any saved bounds
    // exist. DetachableWindow's own restore logic takes over from the
    // second launch on.
    juce::Rectangle<int> defaultMainWindowBounds()
    {
        return juce::Rectangle<int>(900, 620).withCentre(WindowLayoutStore::primaryDisplayArea().getCentre());
    }
}

MainWindow::MainWindow(const juce::String& name, AppSettings& settingsToUse)
    // Setup is shown on its own, with every layout window hidden, so
    // while it's up it IS the main window - it gets a minimise button on
    // the same reasoning the Player window does. It needs no
    // satellite-carrying logic precisely because nothing else is showing.
    : DetachableWindow(name, "main", "Setup", settingsToUse, defaultMainWindowBounds(), true,
                        juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
{
    setVisible(true);
}

void MainWindow::showSetupView(AppSettings& settings, bool isFirstRun,
                                std::function<void(SetupComponent::Result)> onSaveAndLaunch,
                                std::function<void(juce::String, SetupComponent::AuthoriseCallback)> onAuthoriseRpc,
                                SetupComponent::ApplySkinCallback onApplySkin)
{
    setContentOwned(new SetupComponent(settings, isFirstRun, std::move(onSaveAndLaunch),
                                        std::move(onAuthoriseRpc), std::move(onApplySkin)), true);
}

void MainWindow::closeButtonPressed()
{
    // Quitting is only right on FIRST RUN, when this window is the whole
    // app. Once the player is up, Settings merely floats over it, and
    // this unconditional quit meant closing Settings with the X took the
    // whole program down with it - see onCloseRequested.
    if (onCloseRequested != nullptr)
    {
        onCloseRequested();
        return;
    }

    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
