#include "SoundboardWindow.h"

#include "WindowLayoutStore.h"

namespace
{
    juce::Rectangle<int> defaultSoundboardBounds()
    {
        auto area = WindowLayoutStore::primaryDisplayArea();
        return juce::Rectangle<int>(560, 420).withPosition(area.getX() + 700, area.getY() + 540);
    }
}

SoundboardWindow::SoundboardWindow(AppSettings& settingsToUse, SoundboardEngine& soundboard,
                                    SoundboardLayout& soundboardLayout, std::function<void()> onLayoutChanged)
    // defaultVisible = false: a hideable satellite should open on a
    // clean first launch, not clutter the screen unasked.
    : DetachableWindow("Soundboard", "soundboard", settingsToUse, defaultSoundboardBounds(), false)
{
    auto* grid = new SoundboardGridComponent(soundboard, soundboardLayout, std::move(onLayoutChanged));
    grid->setSize(560, 420);

    // false - see PlayerWindow.cpp for why: the window's own (restored
    // or default) bounds must not be overridden by the content's fixed
    // setSize() above.
    setContentOwned(grid, false);
    setVisible(wasVisibleWhenSaved());
}
