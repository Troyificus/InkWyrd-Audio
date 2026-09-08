#include "DetachableWindow.h"

#include "WindowLayoutStore.h"

juce::Array<DetachableWindow*> DetachableWindow::activeWindows;

namespace
{
    // How long to wait after the last move/resize before actually
    // writing to disk. A live drag fires moved() many times a second;
    // this coalesces a whole drag gesture into one write shortly after
    // it settles rather than one per pixel. Updating AppSettings' own
    // in-memory value (setWindowLayoutJson) is cheap and happens every
    // time regardless - only the disk write (settings.save()) is
    // debounced.
    constexpr int kSaveDebounceMs = 800;
}

DetachableWindow::DetachableWindow(const juce::String& windowName, juce::String windowIdToUse,
                                    AppSettings& settingsToUse, juce::Rectangle<int> defaultBounds,
                                    bool defaultVisible,
                                    int requiredButtons)
    : DocumentWindow(windowName,
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour(juce::ResizableWindow::backgroundColourId),
                      requiredButtons),
      windowId(std::move(windowIdToUse)),
      settings(settingsToUse),
      restoredVisible(defaultVisible)
{
    activeWindows.add(this);
    setResizable(true, true);

    auto saved = WindowLayoutStore::fromJson(settings.getWindowLayoutJson());
    auto it = saved.find(windowId);

    auto bounds = defaultBounds;
    if (it != saved.end())
    {
        bounds = WindowLayoutStore::clampToNearestDisplay(it->second.bounds);
        restoredVisible = it->second.visible;
    }

    setBounds(bounds);
}

DetachableWindow::~DetachableWindow()
{
    stopTimer();
    activeWindows.removeFirstMatchingValue(this);
}

void DetachableWindow::userTriedToMoveWindow(juce::Rectangle<int> newBounds)
{
    juce::Array<juce::Rectangle<int>> obstacles;
    for (auto* win : activeWindows)
    {
        if (win != this && win->isVisible() && !win->isMinimised())
        {
            obstacles.add(win->getBounds());
        }
    }

    auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(newBounds);
    auto screenArea = (display != nullptr)
                          ? display->userArea
                          : juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;

    constexpr int kSnapThreshold = 12;
    auto snapped = inkwyrd::snapRectangle(newBounds, obstacles, screenArea, kSnapThreshold);

    setBounds(snapped);
}

void DetachableWindow::closeButtonPressed()
{
    setVisible(false);
}

void DetachableWindow::moved()
{
    DocumentWindow::moved();
    startTimer(kSaveDebounceMs);
}

void DetachableWindow::resized()
{
    DocumentWindow::resized();
    startTimer(kSaveDebounceMs);
}

void DetachableWindow::visibilityChanged()
{
    DocumentWindow::visibilityChanged();
    // Not debounced like moved()/resized() - a show/hide toggle is a
    // single deliberate click, not a continuous stream of events, so
    // there's no flood to coalesce and no reason to delay it.
    persistNow();
}

void DetachableWindow::timerCallback()
{
    stopTimer();
    persistNow();
}

void DetachableWindow::persistNow()
{
    auto states = WindowLayoutStore::fromJson(settings.getWindowLayoutJson());
    states[windowId] = { getBounds(), isVisible() };
    settings.setWindowLayoutJson(WindowLayoutStore::toJson(states));
    settings.save();
}
