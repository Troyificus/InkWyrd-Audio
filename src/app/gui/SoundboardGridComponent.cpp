#include "SoundboardGridComponent.h"

#include "InkwyrdTheme.h"

#include "Dialogs.h"
#include "VolumeCallout.h"

namespace
{
    constexpr int kMinCellWidth = 120;
    constexpr int kCellHeight = 72;
    constexpr int kCellGap = 6;
    constexpr int kCaptionHeight = 26;
    constexpr int kHintHeight = 18;
    constexpr int kSlotStep = 8; // how many slots the +/- buttons add or remove

    constexpr int kVolumeBarHeight = 6;
    constexpr int kVolumeBarBottomInset = 6;
    constexpr int kVolumeBarSideInset = 8;

    // A small fixed palette rather than a full ColourSelector: on a board
    // meant to be scanned at a glance mid-session, a handful of clearly
    // distinct colours is more useful than a colour wheel.
    struct PresetColour { const char* name; juce::uint32 argb; };

    const PresetColour kPresetColours[] = {
        { "Slate",  0xff2a3a33 },
        { "Red",    0xff8c2f2f },
        { "Orange", 0xff9c5a1e },
        { "Yellow", 0xff8a7a1e },
        { "Green",  0xff2f7f52 },
        { "Teal",   0xff1e6b6b },
        { "Blue",   0xff2f4a8c },
        { "Purple", 0xff5a2f8c },
    };

    // Where a gain sits within the trim range, 0..1, for drawing the bar.
    float gainFractionFor(float gainDb)
    {
        return juce::jlimit(0.0f, 1.0f,
                             (gainDb - SoundboardLayout::kMinGainDb)
                              / (SoundboardLayout::kMaxGainDb - SoundboardLayout::kMinGainDb));
    }

    // Draws one of the little level bars, with a tick at unity.
    //
    // The tick matters: the range is -24..+6 dB, so an UNTOUCHED item
    // sits at 80% of the bar and would otherwise read as "turned up
    // loud" rather than "normal". Against the tick it reads as "at the
    // mark", and anything trimmed reads as off it.
    void drawGainBar(juce::Graphics& g, juce::Rectangle<float> bar, float gainDb,
                      float unityFraction)
    {
        g.setColour(inkwyrd::theme::background.withAlpha(0.75f));
        g.fillRoundedRectangle(bar, 2.0f);

        // Boost keeps the warning colour rather than becoming another
        // green - it's the one state here that can clip.
        auto untouched = juce::approximatelyEqual(gainDb, 0.0f);
        g.setColour(gainDb > 0.0f ? inkwyrd::theme::warning.withAlpha(0.9f)
                                   : inkwyrd::theme::accent.withAlpha(untouched ? 0.4f : 0.9f));
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * gainFractionFor(gainDb)), 2.0f);

        auto tickX = bar.getX() + bar.getWidth() * unityFraction;
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.fillRect(juce::Rectangle<float>(tickX - 0.5f, bar.getY() - 1.0f, 1.0f, bar.getHeight() + 2.0f));

        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.drawRoundedRectangle(bar, 2.0f, 1.0f);
    }
}

//==============================================================================
// A button that paints itself: an optional background image, the sound's
// name, and a thin volume bar along the bottom.
//
// Not a TextButton, because none of that survives the LookAndFeel's own
// painting. It also has to distinguish three different presses on the
// same rectangle - fire the sound, open the slot menu, adjust the volume
// - which a plain Button::onClick can't express.
class SoundboardGridComponent::SlotButton final : public juce::Button
{
public:
    SlotButton(int slotIndex,
                std::function<void(int)> onRightClickToUse,
                std::function<void(int)> onVolumeClickToUse,
                std::function<void(int)> onDragStartToUse)
        : juce::Button({}),
          index(slotIndex),
          onRightClick(std::move(onRightClickToUse)),
          onVolumeClick(std::move(onVolumeClickToUse)),
          onDragStart(std::move(onDragStartToUse))
    {
    }

    void setLoopState(bool slotLoops, bool loopIsPlaying)
    {
        if (loops == slotLoops && playing == loopIsPlaying)
            return;

        loops = slotLoops;
        playing = loopIsPlaying;
        repaint();
    }

    void setAppearance(const juce::String& text,
                        juce::Colour backgroundToUse,
                        juce::Colour foregroundToUse,
                        bool showVolumeBar,
                        float gainDbToUse,
                        juce::Image backgroundImageToUse)
    {
        setButtonText(text);
        background = backgroundToUse;
        foreground = foregroundToUse;
        showVolume = showVolumeBar;
        gainDb = gainDbToUse;
        backgroundImage = std::move(backgroundImageToUse);
        repaint();
    }

    juce::Rectangle<int> getVolumeBarBounds() const
    {
        auto r = getLocalBounds().reduced(kVolumeBarSideInset, 0);
        r = r.removeFromBottom(kVolumeBarHeight + kVolumeBarBottomInset);
        r.removeFromBottom(kVolumeBarBottomInset);
        return r;
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto bounds = getLocalBounds().toFloat();
        constexpr float corner = 4.0f;

        if (backgroundImage.isValid())
        {
            juce::Graphics::ScopedSaveState saved(g);

            juce::Path clip;
            clip.addRoundedRectangle(bounds, corner);
            g.reduceClipRegion(clip);

            g.drawImage(backgroundImage, bounds, juce::RectanglePlacement::fillDestination);

            // Darken it: the point of the picture is recognising the
            // button at a glance, and an unreadable label defeats that.
            g.setColour(juce::Colours::black.withAlpha(0.45f));
            g.fillRoundedRectangle(bounds, corner);
        }
        else
        {
            g.setColour(background);
            g.fillRoundedRectangle(bounds, corner);
        }

        if (down || highlighted)
        {
            g.setColour(juce::Colours::white.withAlpha(down ? 0.18f : 0.08f));
            g.fillRoundedRectangle(bounds, corner);
        }

        // A running loop is outlined in the accent colour rather than
        // merely marked: at a glance, the question is "what is playing
        // right now", not "which buttons could loop".
        if (playing)
        {
            g.setColour(inkwyrd::theme::accent);
            g.drawRoundedRectangle(bounds.reduced(1.0f), corner, 2.0f);
        }
        else
        {
            g.setColour(juce::Colours::white.withAlpha(0.25f));
            g.drawRoundedRectangle(bounds.reduced(0.5f), corner, 1.0f);
        }

        if (loops)
        {
            // The loop mark: two arrowheads on a ring, drawn small in
            // the top-left where no label sits.
            auto mark = juce::Rectangle<float>(6.0f, 5.0f, 10.0f, 10.0f);
            g.setColour((playing ? inkwyrd::theme::accent : foreground).withAlpha(playing ? 1.0f : 0.6f));
            g.drawEllipse(mark, 1.4f);
            g.fillRect(mark.getCentreX() - 1.0f, mark.getY() - 1.0f, 4.0f, 2.2f);
        }

        auto textArea = getLocalBounds().reduced(6);
        if (showVolume)
            textArea.removeFromBottom(kVolumeBarHeight + kVolumeBarBottomInset);

        g.setColour(foreground);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawFittedText(getButtonText(), textArea, juce::Justification::centred, 3, 0.8f);

        if (!showVolume)
            return;

        // Amber above unity, so "this one is boosted" reads differently
        // from "this one is turned down".
        drawGainBar(g, getVolumeBarBounds().toFloat(), gainDb, gainFractionFor(0.0f));
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            // Deliberately NOT calling through to Button: letting it
            // register a press here would fire the sound as well as open
            // the menu.
            if (onRightClick)
                onRightClick(index);

            return;
        }

        // The bar is only a few pixels tall, so its hit area is taller
        // than it looks - otherwise it's a game of pixel-hunting during
        // a session.
        if (showVolume && getVolumeBarBounds().expanded(0, 6).contains(event.getPosition()))
        {
            if (onVolumeClick)
                onVolumeClick(index);

            return;
        }

        juce::Button::mouseDown(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        // Past a threshold, so an ordinary press with a shaky hand still
        // fires the sound instead of picking the button up.
        if (dragged || event.getDistanceFromDragStart() <= 8 || onDragStart == nullptr)
            return;

        dragged = true;
        onDragStart(index);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        // A button that was just dragged must not ALSO fire its sound on
        // the way down - rearranging a board mid-session would otherwise
        // play half of it into the call.
        if (dragged)
        {
            dragged = false;
            setState(buttonNormal);
            return;
        }

        juce::Button::mouseUp(event);
    }

    int getSlotIndex() const { return index; }

private:
    int index;
    std::function<void(int)> onRightClick, onVolumeClick, onDragStart;
    bool loops = false, playing = false;

    // Whether this press turned into a drag, so mouseUp knows not to
    // treat it as a click.
    bool dragged = false;

    juce::Colour background { 0xff2a3a33 }, foreground { juce::Colours::white };
    bool showVolume = false;
    float gainDb = 0.0f;
    juce::Image backgroundImage;
};

//==============================================================================
SoundboardGridComponent::SoundboardGridComponent(SoundboardEngine& soundboardToUse,
                                                  SoundboardLayout& layoutToUse,
                                                  std::function<void()> onLayoutChangedToUse)
    : soundboard(soundboardToUse),
      layout(layoutToUse),
      onLayoutChanged(std::move(onLayoutChangedToUse))
{
    addAndMakeVisible(caption);

    importButton.onClick = [this] { importFolderIntoBoard(); };
    stopAllButton.onClick = [this] { soundboard.stopAllVoices(); };
    addSlotsButton.onClick = [this] { changeSlotCount(kSlotStep); };
    removeSlotsButton.onClick = [this] { changeSlotCount(-kSlotStep); };

    for (auto* button : { &stopAllButton, &importButton, &addSlotsButton, &removeSlotsButton })
        addAndMakeVisible(button);

    hint.setText("Click an empty button to assign a sound, or drag files in. Drag a button onto "
                  "another to swap them. Click a button's volume bar to adjust it. Right-click to "
                  "rename, recolour, loop, add a picture or clear. Stop all (or Esc on the Player) "
                  "silences every sound playing.",
                  juce::dontSendNotification);
    hint.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(hint);

    // Slow on purpose: this only has to notice a loop starting or
    // stopping, which is a thing a person did, not an audio-rate event.
    startTimerHz(4);

    viewport.setViewedComponent(&gridPanel, false);
    lookAndFeelChanged();
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    refresh();
}

SoundboardGridComponent::~SoundboardGridComponent() = default;

void SoundboardGridComponent::refresh()
{
    rebuildButtons();
    resized();
}

void SoundboardGridComponent::notifyChanged()
{
    if (onLayoutChanged)
        onLayoutChanged();

    refresh();
}

juce::Image SoundboardGridComponent::imageFor(const juce::File& file)
{
    if (file == juce::File() || !file.existsAsFile())
        return {};

    auto key = file.getFullPathName().toLowerCase();
    auto cached = imageCache.find(key);
    if (cached != imageCache.end())
        return cached->second;

    auto loaded = juce::ImageFileFormat::loadFrom(file);
    imageCache[key] = loaded; // cached even when invalid, so a bad file isn't retried every repaint
    return loaded;
}

void SoundboardGridComponent::applyAppearance(int index)
{
    if (!juce::isPositiveAndBelow(index, buttons.size()))
        return;

    const auto& slot = layout.getSlot(index);
    auto* button = buttons[index];

    if (slot.isEmpty())
    {
        button->setAppearance("+", inkwyrd::theme::panelRaised, inkwyrd::theme::textDim, false, 0.0f, {});
        return;
    }

    auto missing = !slot.file.existsAsFile();
    button->setLoopState(slot.loop && ! missing, slot.loop && soundboard.isPlaying(slot.name));

    // Say so on the button itself. A sound that silently does nothing
    // when pressed mid-session is the worst outcome here.
    button->setAppearance(missing ? slot.name + " (file missing)" : slot.name,
                           missing ? juce::Colour(0xff4a3030) : juce::Colour(slot.colourArgb),
                           missing ? juce::Colours::lightgrey : juce::Colours::white,
                           !missing,
                           slot.gainDb,
                           missing ? juce::Image() : imageFor(slot.imageFile));
}

void SoundboardGridComponent::rebuildButtons()
{
    buttons.clear();

    for (int i = 0; i < layout.getNumSlots(); ++i)
    {
        auto* button = buttons.add(new SlotButton(i,
                                                   [this](int index) { slotRightClicked(index); },
                                                   [this](int index) { showVolumeCallout(index); },
                                                   [this](int index) { startSlotDrag(index); }));
        gridPanel.addAndMakeVisible(button);
        button->onClick = [this, i] { slotClicked(i); };
        applyAppearance(i);
    }

    removeSlotsButton.setEnabled(layout.getNumSlots() > 1);
}

void SoundboardGridComponent::slotClicked(int index)
{
    const auto& slot = layout.getSlot(index);

    if (slot.isEmpty())
    {
        assignToSlot(index);
        return;
    }

    // Guarded rather than assumed: the file can go missing between
    // launches, in which case the engine never registered it.
    if (soundboard.hasSound(slot.name))
        soundboard.trigger(slot.name);
}

void SoundboardGridComponent::showVolumeCallout(int index)
{
    if (!layout.isValidIndex(index) || !juce::isPositiveAndBelow(index, buttons.size()))
        return;

    const auto& slot = layout.getSlot(index);

    auto content = std::make_unique<VolumeCallout>(slot.name, slot.gainDb,
                                                    SoundboardLayout::kMinGainDb,
                                                    SoundboardLayout::kMaxGainDb,
        [this, safeThis = juce::Component::SafePointer<SoundboardGridComponent>(this), index](float db)
    {
        if (safeThis == nullptr)
            return;

        layout.setGainDb(index, db);

        // Deliberately NOT notifyChanged(): that rebuilds every button,
        // which would destroy the one this callout is anchored to while
        // it's still open. Re-register with the engine and repaint just
        // this button instead.
        if (onLayoutChanged)
            onLayoutChanged();

        applyAppearance(index);
    });

    juce::CallOutBox::launchAsynchronously(std::move(content),
                                            getLocalArea(&gridPanel, buttons[index]->getBounds()),
                                            this);
}

void SoundboardGridComponent::slotRightClicked(int index)
{
    if (!layout.isValidIndex(index))
        return;

    const auto& slot = layout.getSlot(index);
    auto empty = slot.isEmpty();
    auto hasImage = slot.imageFile != juce::File();
    const int numPresets = (int) juce::numElementsInArray(kPresetColours);

    juce::PopupMenu menu;

    if (empty)
    {
        menu.addItem(1, "Assign sound...");
    }
    else
    {
        menu.addItem(1, "Replace sound...");
        menu.addItem(2, "Rename...");
        menu.addItem(6, "Volume...");
        menu.addItem(7, "Loop this sound", true, slot.loop);

        juce::PopupMenu colours;
        for (int i = 0; i < numPresets; ++i)
            colours.addItem(100 + i, kPresetColours[i].name);

        menu.addSubMenu("Colour", colours);
        menu.addItem(4, hasImage ? "Change picture..." : "Set a picture...");
        menu.addItem(5, "Remove picture", hasImage);
        menu.addSeparator();
        menu.addItem(3, "Clear this button");
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(buttons[index]),
                        [this, safeThis = juce::Component::SafePointer<SoundboardGridComponent>(this),
                         index, numPresets](int result)
    {
        if (result == 0 || safeThis == nullptr)
            return;

        if (result == 1)
            assignToSlot(index);
        else if (result == 2)
            renameSlot(index);
        else if (result == 3)
            clearSlot(index);
        else if (result == 4)
            chooseImageForSlot(index);
        else if (result == 5)
        {
            layout.clearImage(index);
            refresh(); // picture is cosmetic - nothing to re-register
        }
        else if (result == 6)
        {
            showVolumeCallout(index);
        }
        else if (result == 7)
        {
            toggleLoop(index);
        }
        else if (result >= 100 && result < 100 + numPresets)
        {
            layout.setColour(index, kPresetColours[result - 100].argb);
            // Colour is cosmetic - the engine's registration is by name
            // and file, so this only needs a repaint, not a re-register.
            refresh();
        }
    });
}

void SoundboardGridComponent::assignToSlot(int index)
{
    activeChooser = std::make_unique<juce::FileChooser>("Choose a sound for this button");
    activeChooser->launchAsync(juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::canSelectMultipleItems,
                                [this, safeThis = juce::Component::SafePointer<SoundboardGridComponent>(this), index]
                                (const juce::FileChooser& chooser)
    {
        auto results = chooser.getResults();
        if (results.isEmpty() || safeThis == nullptr)
            return; // the board was replaced (Settings) while the chooser was open

        // Selecting several files fills this button and then the free
        // ones after it, rather than making the user repeat this eight
        // times to set a board up.
        if (layout.assignFrom(index, results) == 0)
        {
            inkwyrd::showMessage(safeThis, juce::MessageBoxIconType::WarningIcon,
                                  "Couldn't add that",
                                  "That file isn't in a format this app can play. Supported: WAV, "
                                  "AIFF, FLAC, Ogg Vorbis, MP3, AAC/M4A and WMA.");
            return;
        }

        notifyChanged();
    });
}

void SoundboardGridComponent::chooseImageForSlot(int index)
{
    activeChooser = std::make_unique<juce::FileChooser>("Choose a picture for this button", juce::File(),
                                                         "*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp");
    activeChooser->launchAsync(juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles,
                                [this, safeThis = juce::Component::SafePointer<SoundboardGridComponent>(this), index]
                                (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File() || safeThis == nullptr)
            return;

        if (!layout.setImage(index, file))
        {
            inkwyrd::showMessage(safeThis, juce::MessageBoxIconType::WarningIcon,
                                  "Not a picture",
                                  "That file isn't an image this app recognises. Supported: PNG, "
                                  "JPEG, GIF, BMP and WebP.");
            return;
        }

        refresh(); // cosmetic only - nothing to re-register with the engine
    });
}

void SoundboardGridComponent::renameSlot(int index)
{
    auto* window = new juce::AlertWindow("Rename button", "New name:",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", layout.getSlot(index).name);
    window->addButton("Rename", 1);
    window->addButton("Cancel", 0);

    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, safeThis = juce::Component::SafePointer<SoundboardGridComponent>(this), index, window](int result)
    {
        std::unique_ptr<juce::AlertWindow> owned(window); // deleted however we leave here
        if (result != 1 || safeThis == nullptr)
            return;

        if (!layout.rename(index, owned->getTextEditorContents("name")))
        {
            inkwyrd::showMessage(safeThis, juce::MessageBoxIconType::WarningIcon,
                                  "Couldn't rename",
                                  "That name is either empty or already used by another button. "
                                  "Names have to be unique - the name is what a Stream Deck button "
                                  "sends to trigger the sound.");
            return;
        }

        // The name IS the engine's key, so this genuinely has to
        // re-register rather than just repaint.
        notifyChanged();
    }));
}

void SoundboardGridComponent::clearSlot(int index)
{
    layout.clearSlot(index);
    notifyChanged();
}

void SoundboardGridComponent::importFolderIntoBoard()
{
    activeChooser = std::make_unique<juce::FileChooser>("Import a folder of sound effects");
    activeChooser->launchAsync(juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectDirectories,
                                [this, safeThis = juce::Component::SafePointer<SoundboardGridComponent>(this)]
                                (const juce::FileChooser& chooser)
    {
        auto folder = chooser.getResult();
        if (folder == juce::File() || !folder.isDirectory() || safeThis == nullptr)
            return;

        auto imported = layout.importFolder(folder);
        notifyChanged();

        if (imported == 0)
            inkwyrd::showMessage(safeThis, juce::MessageBoxIconType::InfoIcon,
                                  "Nothing to import",
                                  "That folder has no playable audio files that aren't already on "
                                  "the board.");
    });
}

void SoundboardGridComponent::changeSlotCount(int delta)
{
    auto wanted = layout.getNumSlots() + delta;
    auto settled = layout.setNumSlots(wanted);

    // setNumSlots refuses to drop a slot that has a sound in it, so this
    // can legitimately do less than it was asked to.
    if (delta < 0 && settled > wanted)
        inkwyrd::showMessage(this, juce::MessageBoxIconType::InfoIcon,
                              "Buttons still in use",
                              "Only empty buttons at the end of the board can be removed. Clear the "
                              "sounds off them first.");

    refresh();
}

//==============================================================================
// Drag and drop from Explorer, onto a specific button.

bool SoundboardGridComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        juce::File file(path);
        if (!file.isDirectory() && file.existsAsFile())
            return true;
    }

    return false;
}

void SoundboardGridComponent::fileDragEnter(const juce::StringArray&, int x, int y)
{
    dragActive = true;
    dragTargetSlot = slotIndexAt(x, y);
    repaint();
}

void SoundboardGridComponent::fileDragMove(const juce::StringArray&, int x, int y)
{
    auto slot = slotIndexAt(x, y);
    if (slot == dragTargetSlot)
        return;

    dragTargetSlot = slot;
    repaint();
}

void SoundboardGridComponent::fileDragExit(const juce::StringArray&)
{
    dragActive = false;
    dragTargetSlot = -1;
    repaint();
}

void SoundboardGridComponent::startSlotDrag(int index)
{
    // An empty slot has nothing to move, and a drag already running must
    // not start a second one.
    if (! layout.isValidIndex(index) || layout.getSlot(index).isEmpty()
        || isDragAndDropActive() || ! juce::isPositiveAndBelow(index, buttons.size()))
        return;

    // JUCE's own internal drag, not an OS file drag: this one never
    // leaves the component, and the description is just which slot was
    // picked up.
    startDragging(juce::var(index), buttons[index]);
}

bool SoundboardGridComponent::isInterestedInDragSource(const SourceDetails& details)
{
    // Only this board's own buttons. Anything else dragged over the
    // window is somebody else's business.
    return details.sourceComponent != nullptr && isParentOf(details.sourceComponent.get())
            && details.description.isInt();
}

void SoundboardGridComponent::itemDragEnter(const SourceDetails& details)
{
    dragActive = true;
    dragTargetSlot = slotIndexAt(details.localPosition.x, details.localPosition.y);
    repaint();
}

void SoundboardGridComponent::itemDragMove(const SourceDetails& details)
{
    auto slot = slotIndexAt(details.localPosition.x, details.localPosition.y);
    if (slot == dragTargetSlot)
        return;

    dragTargetSlot = slot;
    repaint();
}

void SoundboardGridComponent::itemDragExit(const SourceDetails&)
{
    dragActive = false;
    dragTargetSlot = -1;
    repaint();
}

void SoundboardGridComponent::itemDropped(const SourceDetails& details)
{
    auto from = (int) details.description;
    auto to = slotIndexAt(details.localPosition.x, details.localPosition.y);

    dragActive = false;
    dragTargetSlot = -1;

    // Dropped on itself, or on the gap between buttons: nothing to do,
    // and silently putting it somewhere else would be worse.
    if (to < 0 || to == from)
    {
        repaint();
        return;
    }

    if (layout.swapSlots(from, to))
        notifyChanged(); // names are unchanged, so nothing a Stream Deck sends breaks
}

void SoundboardGridComponent::toggleLoop(int index)
{
    if (! layout.isValidIndex(index))
        return;

    const auto& slot = layout.getSlot(index);
    auto nowLooping = ! slot.loop;

    // Stop it first if it is running: a sound that was started as a loop
    // is playing a looping voice, and turning the flag off is a request
    // for it to stop rather than for it to run on forever unmarked.
    if (! nowLooping && soundboard.isPlaying(slot.name))
        soundboard.stop(slot.name);

    layout.setLoop(index, nowLooping);
    notifyChanged(); // the engine keys looping per registered sound
}

void SoundboardGridComponent::timerCallback()
{
    // A loop can stop for reasons this component never sees: the panic
    // button, a Stream Deck press, or the Voice FX window's own Stop.
    auto playingNow = soundboard.getPlayingLoopNames();
    if (playingNow == playingLoops)
        return;

    playingLoops = playingNow;

    for (int i = 0; i < buttons.size() && layout.isValidIndex(i); ++i)
        applyAppearance(i);
}

int SoundboardGridComponent::slotIndexAt(int x, int y) const
{
    // x, y arrive in THIS component's coordinate space (JUCE converts via
    // getLocalPoint before calling), so they have to be taken into the
    // scrolled grid panel's space before hit-testing the buttons.
    auto inGrid = gridPanel.getLocalPoint(this, juce::Point<int>(x, y));

    for (auto* button : buttons)
        if (button->getBounds().contains(inGrid))
            return button->getSlotIndex();

    return -1;
}

void SoundboardGridComponent::paintOverChildren(juce::Graphics& g)
{
    if (!dragActive)
        return;

    g.setColour(inkwyrd::theme::accent);

    if (juce::isPositiveAndBelow(dragTargetSlot, buttons.size()))
    {
        // Show exactly which button the sound is about to land on.
        g.drawRect(getLocalArea(&gridPanel, buttons[dragTargetSlot]->getBounds()), 2);
    }
    else
    {
        g.drawRect(getLocalBounds(), 2);
    }
}

void SoundboardGridComponent::filesDropped(const juce::StringArray& paths, int x, int y)
{
    auto target = slotIndexAt(x, y);
    dragActive = false;
    dragTargetSlot = -1;
    repaint();

    juce::Array<juce::File> audioFiles;
    juce::File droppedImage;

    for (const auto& path : paths)
    {
        juce::File file(path);
        if (!file.existsAsFile())
            continue;

        // An image dropped on a button is unambiguous - it can only mean
        // "use this as the picture" - so it doesn't need a menu.
        if (inkwyrd::isImageFile(file))
        {
            if (droppedImage == juce::File())
                droppedImage = file;
        }
        else
        {
            audioFiles.add(file);
        }
    }

    if (audioFiles.isEmpty() && droppedImage != juce::File())
    {
        if (target < 0 || layout.getSlot(target).isEmpty())
        {
            inkwyrd::showMessage(this, juce::MessageBoxIconType::InfoIcon,
                                  "Drop it on a sound",
                                  "Pictures are backgrounds for a button that already has a sound on "
                                  "it. Assign a sound first, then drop the picture onto that button.");
            return;
        }

        layout.setImage(target, droppedImage);
        refresh();
        return;
    }

    if (audioFiles.isEmpty())
        return;

    // Dropped in the gap between buttons: fall back to the first free
    // slot rather than discarding the drop.
    if (target < 0)
    {
        for (int i = 0; i < layout.getNumSlots() && target < 0; ++i)
            if (layout.getSlot(i).isEmpty())
                target = i;

        if (target < 0)
            target = layout.getNumSlots(); // past the end - assignFrom grows the board
    }

    if (layout.assignFrom(target, audioFiles) == 0)
    {
        inkwyrd::showMessage(this, juce::MessageBoxIconType::WarningIcon,
                              "Nothing to add",
                              "None of those files are in a format this app can play. Supported: "
                              "WAV, AIFF, FLAC, Ogg Vorbis, MP3, AAC/M4A and WMA.");
        return;
    }

    // A sound and its picture dropped together in one go.
    if (droppedImage != juce::File() && layout.isValidIndex(target))
        layout.setImage(target, droppedImage);

    notifyChanged();
}

//==============================================================================
void SoundboardGridComponent::resized()
{
    auto area = getLocalBounds();

    auto captionRow = area.removeFromTop(kCaptionHeight);
    removeSlotsButton.setBounds(captionRow.removeFromRight(30).reduced(0, 2));
    captionRow.removeFromRight(4);
    addSlotsButton.setBounds(captionRow.removeFromRight(30).reduced(0, 2));
    captionRow.removeFromRight(4);
    importButton.setBounds(captionRow.removeFromRight(120).reduced(0, 2));
    captionRow.removeFromRight(4);
    stopAllButton.setBounds(captionRow.removeFromRight(80).reduced(0, 2));
    caption.setBounds(captionRow);

    hint.setBounds(area.removeFromTop(kHintHeight));
    area.removeFromTop(4);

    viewport.setBounds(area);
    layOutGrid();
}

void SoundboardGridComponent::layOutGrid()
{
    auto area = viewport.getBounds();

    auto usableWidth = juce::jmax(kMinCellWidth, area.getWidth() - viewport.getScrollBarThickness());
    auto columns = juce::jmax(1, (usableWidth + kCellGap) / (kMinCellWidth + kCellGap));
    auto cellWidth = (usableWidth - (columns - 1) * kCellGap) / columns;

    int rows = (buttons.size() + columns - 1) / columns;
    gridPanel.setSize(usableWidth, juce::jmax(kCellHeight, rows * (kCellHeight + kCellGap)));

    for (int i = 0; i < buttons.size(); ++i)
    {
        int column = i % columns;
        int row = i / columns;
        buttons[i]->setBounds(column * (cellWidth + kCellGap),
                               row * (kCellHeight + kCellGap),
                               cellWidth,
                               kCellHeight);
    }
}

// Label colours are COPIES of the palette taken when the component is
// built, so a skin change has to re-apply them - JUCE calls this on
// every child when a window sends a look-and-feel change.
void SoundboardGridComponent::lookAndFeelChanged()
{
    hint.setColour(juce::Label::textColourId, inkwyrd::theme::textDim);

    // An EMPTY pad is painted in the palette's colours, but a pad holds
    // the colours it was given rather than reading them each paint (a
    // filled pad's colour is the user's own choice, stored per slot). So
    // a skin change has to hand them out again.
    for (int i = 0; i < layout.getNumSlots(); ++i)
        applyAppearance(i);
}
