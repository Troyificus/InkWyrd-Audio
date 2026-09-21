#include "ScenesWindow.h"

#include "WindowLayoutStore.h"

namespace
{
    juce::Rectangle<int> defaultScenesBounds()
    {
        auto area = WindowLayoutStore::primaryDisplayArea();
        return juce::Rectangle<int>(560, 240).withPosition(area.getX() + 700, area.getY() + 290);
    }
}

//==============================================================================
ScenesWindow::ScenesWindow(AppSettings& settingsToUse, SceneLibrary& library, ScenesComponent::Callbacks callbacks)
    // defaultVisible = false, like the other satellites: it opens on a
    // clean first launch only when asked for.
    : DetachableWindow("Scenes", "scenes", "Scenes", settingsToUse, defaultScenesBounds(), false)
{
    auto* component = new ScenesComponent(library, std::move(callbacks));
    component->setSize(560, 240);
    scenes = component;

    // false - see PlayerWindow.cpp: the restored bounds must win over the
    // content's own size.
    setContentOwned(component, false);
    setVisible(wasVisibleWhenSaved());
}
