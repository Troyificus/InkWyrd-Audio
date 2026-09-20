#pragma once

#include <functional>
#include <map>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "SoundboardEngine.h"
#include "SoundboardLayout.h"

// The SFX board: a reflowing grid of assignable, Stream-Deck-style
// buttons backed by SoundboardLayout.
//
// Every button is a fixed SLOT, filled or empty. Left-click a filled one
// to fire it, an empty one to assign a sound; right-click for rename,
// colour, background image, volume, replace and clear. Audio files can
// also be dragged straight in from Explorer onto a specific slot, and so
// can an image, which becomes that button's background.
//
// Each filled button carries a thin volume bar along its bottom edge.
// Clicking the BAR (rather than the button) opens a slider instead of
// firing the sound - the bar is a live readout the rest of the time, so
// a button that has been turned down says so at a glance.
//
// The layout owns the truth and saves itself on every change; this
// component just drives it and calls onLayoutChanged() so the app can
// re-register the sounds with the engine.
class SoundboardGridComponent : public juce::Component,
                                 public juce::FileDragAndDropTarget
{
public:
    SoundboardGridComponent(SoundboardEngine& soundboardToUse,
                             SoundboardLayout& layoutToUse,
                             std::function<void()> onLayoutChangedToUse);

    // Defined in the .cpp - SlotButton is forward-declared here and an
    // OwnedArray needs the complete type to destroy it.
    ~SoundboardGridComponent() override;

    // Rebuild the buttons from the layout. Called after anything changes
    // the layout from outside this component (a folder import, say).
    void refresh();

    void resized() override;

    // Re-applies the label colours this component sets for itself,
    // so it follows a skin change like everything drawn from the
    // palette at paint time does.
    void lookAndFeelChanged() override;
    void paintOverChildren(juce::Graphics& g) override;

    // juce::FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    class SlotButton;

    void rebuildButtons();
    void applyAppearance(int index);
    void layOutGrid();

    void slotClicked(int index);
    void slotRightClicked(int index);
    void showVolumeCallout(int index);
    void assignToSlot(int index);
    void chooseImageForSlot(int index);
    void renameSlot(int index);
    void clearSlot(int index);
    void importFolderIntoBoard();
    void changeSlotCount(int delta);

    int slotIndexAt(int x, int y) const;
    void notifyChanged();

    // Decoded once per distinct path rather than on every repaint - a
    // grid of large photographs would otherwise decode them continuously.
    juce::Image imageFor(const juce::File& file);

    SoundboardEngine& soundboard;
    SoundboardLayout& layout;
    std::function<void()> onLayoutChanged;

    juce::Label caption { {}, "Soundboard" };
    juce::TextButton importButton { "Import folder..." };

    // Silences every sound playing right now. Up to 16 can overlap, and
    // one wrong press mid-session had no undo before this.
    juce::TextButton stopAllButton { "Stop all" };
    juce::TextButton addSlotsButton { "+" };
    juce::TextButton removeSlotsButton { "-" };
    juce::Label hint;

    juce::Viewport viewport;
    juce::Component gridPanel;
    juce::OwnedArray<SlotButton> buttons;

    std::map<juce::String, juce::Image> imageCache;

    std::unique_ptr<juce::FileChooser> activeChooser;

    bool dragActive = false;
    int dragTargetSlot = -1;
};
