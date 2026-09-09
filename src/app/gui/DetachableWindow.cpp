#include "DetachableWindow.h"

#include "WindowLayoutStore.h"
#include "WindowSnapping.h"

juce::Array<DetachableWindow*> DetachableWindow::activeWindows;

namespace
{
    // How long after the last moved() to treat a movement burst as over.
    // Short enough to feel immediate on release, long enough not to fire
    // in the middle of a drag that's still tracking the mouse.
    constexpr int kSettleMs = 150;

    // How close an edge has to get before it snaps. 12px is enough to
    // feel magnetic without fighting someone deliberately placing a
    // window a few pixels off another.
    constexpr int kSnapThreshold = 12;

    // Deliberately looser than kSnapThreshold when deciding what counts
    // as "already docked, come along with me". A snapped window is
    // exactly 0px away, but one placed by hand can be a pixel or two out
    // and should still travel with the group.
    constexpr int kDockTolerance = 4;
}

DetachableWindow::DetachableWindow(const juce::String& windowName, juce::String windowIdToUse,
                                    AppSettings& settingsToUse, juce::Rectangle<int> defaultBounds,
                                    bool defaultVisible,
                                    int titleBarButtons)
    : DocumentWindow(windowName,
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour(juce::ResizableWindow::backgroundColourId),
                      titleBarButtons),
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

    // This runs before the subclass makes the window visible, and moved()
    // ignores movement while invisible - so restoring a saved arrangement
    // never snaps it or drags other windows around.
    setBounds(bounds);
    lastMovedPosition = bounds.getPosition();
}

DetachableWindow::~DetachableWindow()
{
    stopTimer();
    activeWindows.removeFirstMatchingValue(this);
}

juce::Array<DetachableWindow*> DetachableWindow::otherLiveWindows() const
{
    juce::Array<DetachableWindow*> result;

    for (auto* window : activeWindows)
        if (window != this && window->isVisible() && ! window->isMinimised())
            result.add(window);

    return result;
}

bool DetachableWindow::isCarrying(const DetachableWindow* window) const
{
    for (auto& member : dockedGroup)
        if (member.getComponent() == window)
            return true;

    return false;
}

void DetachableWindow::captureDockedGroup(juce::Rectangle<int> boundsToTestFrom)
{
    dockedGroup.clear();

    auto others = otherLiveWindows();

    juce::Array<juce::Rectangle<int>> rectangles;
    rectangles.add(boundsToTestFrom); // index 0 is always this window
    for (auto* other : others)
        rectangles.add(other->getBounds());

    for (auto index : inkwyrd::findDockedGroup(rectangles, 0, kDockTolerance))
        dockedGroup.add(others[index - 1]); // -1: index 0 was this window
}

void DetachableWindow::translateDockedGroup(juce::Point<int> delta)
{
    if (delta.isOrigin())
        return;

    for (auto& member : dockedGroup)
        if (auto* window = member.getComponent())
            window->setBounds(window->getBounds().translated(delta.x, delta.y));
}

void DetachableWindow::applySnap()
{
    if (! isVisible() || isMinimised())
        return;

    juce::Array<juce::Rectangle<int>> obstacles;
    for (auto* other : otherLiveWindows())
    {
        // A window being carried along is flush by definition, so letting
        // it act as a magnet would just pin the group where it started.
        if (isCarrying(other))
            continue;

        obstacles.add(other->getBounds());
    }

    auto current = getBounds();

    auto& displays = juce::Desktop::getInstance().getDisplays();
    auto* display = displays.getDisplayForRect(current);
    auto screenArea = display != nullptr ? display->userArea
                                          : WindowLayoutStore::primaryDisplayArea();

    auto snapped = inkwyrd::snapRectangle(current, obstacles, screenArea, kSnapThreshold);
    if (snapped == current)
        return;

    // applyingSnap stops the resulting moved() from being mistaken for
    // the start of a fresh drag (which would re-capture a group and snap
    // again, indefinitely).
    const juce::ScopedValueSetter<bool> scope(applyingSnap, true);
    setBounds(snapped);

    // The group has to travel with the correction too, or landing flush
    // would tear a docked pair apart by up to kSnapThreshold pixels.
    translateDockedGroup(snapped.getPosition() - current.getPosition());
}

void DetachableWindow::setHiddenByMasterMinimise(bool shouldBeHidden)
{
    hiddenByMasterMinimise = shouldBeHidden;
    setVisible(! shouldBeHidden);
}

void DetachableWindow::closeButtonPressed()
{
    setVisible(false);
}

void DetachableWindow::moved()
{
    DocumentWindow::moved();

    auto position = getBounds().getPosition();

    // Skipped while invisible (startup restore) and while applying our
    // own correction - neither is a user drag.
    if (isVisible() && ! applyingSnap)
    {
        if (! movementInProgress)
        {
            movementInProgress = true;

            // Deliberately the PRE-MOVE rectangle, not the current one:
            // by the time this first callback arrives the window has
            // already travelled several pixels, which is enough to stop
            // registering as flush against the neighbour it was docked
            // to a moment ago. lastMovedPosition still holds where it
            // was sitting before this burst started.
            captureDockedGroup(getBounds().withPosition(lastMovedPosition));
        }

        // Applied on the first callback too, so the group catches up on
        // the few pixels the leader had already covered by then.
        translateDockedGroup(position - lastMovedPosition);
    }

    lastMovedPosition = position;

    // Restarted on every move, so it only fires once the window has
    // actually stopped - see kSettleMs.
    startTimer(kSettleMs);
}

void DetachableWindow::resized()
{
    DocumentWindow::resized();
    startTimer(kSettleMs);
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

    if (movementInProgress)
    {
        movementInProgress = false;
        applySnap();
        dockedGroup.clear();
    }

    persistNow();
}

void DetachableWindow::persistNow()
{
    auto states = WindowLayoutStore::fromJson(settings.getWindowLayoutJson());

    // `|| hiddenByMasterMinimise` records what the user actually chose,
    // not the transient hidden state a minimised master imposes - see
    // setHiddenByMasterMinimise(). Without it, quitting while minimised
    // saves every satellite as hidden.
    states[windowId] = { getBounds(), isVisible() || hiddenByMasterMinimise };

    settings.setWindowLayoutJson(WindowLayoutStore::toJson(states));
    settings.save();
}
