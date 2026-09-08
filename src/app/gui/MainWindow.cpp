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
    : DetachableWindow(name, "main", settingsToUse, defaultMainWindowBounds(), true,
                       DocumentWindow::closeButton | DocumentWindow::minimizeButton)
{
    setVisible(true);
}

void MainWindow::showSetupView(AppSettings& settings, bool isFirstRun,
                                std::function<void(SetupComponent::Result)> onSaveAndLaunch)
{
    setContentOwned(new SetupComponent(settings, isFirstRun, std::move(onSaveAndLaunch)), true);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
