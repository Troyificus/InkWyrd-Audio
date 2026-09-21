#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "SceneLibrary.h"

// The Scenes window: a grid of big buttons, one per scene, and a way to
// save what's playing now as a new one.
//
// Its own window rather than a row on the Soundboard, deliberately: a
// scene changes the whole room and a sound effect is momentary, and
// mixing the two in one grid makes it easy to hit the wrong kind of
// button mid-session. The two windows snap together like the others.
//
// This component only draws and reports. Everything a scene actually
// DOES is the app's job - see InkwyrdAudioApplication::activateScene -
// so the rules live in one place (planScene) and not spread through UI.
class ScenesComponent : public juce::Component
{
public:
    struct Callbacks
    {
        std::function<void(const juce::Uuid&)> activate;
        std::function<void()> saveCurrentAsNew;
        std::function<void(const juce::Uuid&)> updateFromCurrent;
        std::function<void(const juce::Uuid&)> edit;
        std::function<void(const juce::Uuid&)> remove;
        std::function<void(const juce::Uuid&, int delta)> move;

        // What is missing from a scene right now, to show on its button.
        std::function<juce::StringArray(const Scene&)> problemsFor;
    };

    ScenesComponent(SceneLibrary& libraryToUse, Callbacks callbacksToUse);
    ~ScenesComponent() override;

    // Rebuilds the buttons from the library. Called after anything that
    // adds, removes, renames or reorders a scene.
    void refresh();

    void setActiveScene(const juce::Uuid& id);

    void resized() override;
    void lookAndFeelChanged() override;

private:
    class SceneButton;

    void showMenuFor(const juce::Uuid& id);
    void layOutGrid();

    SceneLibrary& library;
    Callbacks callbacks;
    juce::Uuid activeId;

    juce::Label caption { {}, "Scenes" };
    juce::TextButton saveCurrentButton { "+ Save current as scene" };
    juce::Label emptyHint;

    juce::Viewport viewport;
    juce::Component gridPanel;
    juce::OwnedArray<SceneButton> buttons;
};
