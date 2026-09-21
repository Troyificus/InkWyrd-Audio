#include "ScenesComponent.h"

#include "InkwyrdTheme.h"

namespace
{
    constexpr int kMinCellWidth = 130;
    constexpr int kCellHeight = 64;
    constexpr int kCellGap = 6;
    constexpr int kHeaderHeight = 28;
}

//==============================================================================
// One scene. Painted by hand, like a soundboard button, because it has to
// show three things a TextButton can't: its colour, whether it is the
// scene in effect, and what (if anything) it has lost.
class ScenesComponent::SceneButton final : public juce::Button
{
public:
    SceneButton(const Scene& sceneToShow, std::function<void()> onRightClickToUse)
        : juce::Button(sceneToShow.name), scene(sceneToShow), onRightClick(std::move(onRightClickToUse))
    {
    }

    const juce::Uuid& getSceneId() const { return scene.id; }

    void setActive(bool shouldBeActive)
    {
        if (active != shouldBeActive)
        {
            active = shouldBeActive;
            repaint();
        }
    }

    void setProblems(const juce::StringArray& problemsToShow)
    {
        problems = problemsToShow;
        setTooltip(problems.isEmpty() ? juce::String()
                                      : "Skipped when pressed: " + problems.joinIntoString("; "));
        repaint();
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto bounds = getLocalBounds().toFloat();
        constexpr float corner = 5.0f;

        g.setColour(juce::Colour(scene.colourArgb));
        g.fillRoundedRectangle(bounds, corner);

        if (down || highlighted)
        {
            g.setColour(juce::Colours::white.withAlpha(down ? 0.18f : 0.08f));
            g.fillRoundedRectangle(bounds, corner);
        }

        // The scene in effect gets the accent outline the soundboard uses
        // for a running loop - the same visual word for "this is live".
        if (active)
        {
            g.setColour(inkwyrd::theme::accent);
            g.drawRoundedRectangle(bounds.reduced(1.0f), corner, 2.5f);
        }
        else
        {
            g.setColour(juce::Colours::white.withAlpha(0.25f));
            g.drawRoundedRectangle(bounds.reduced(0.5f), corner, 1.0f);
        }

        auto textArea = getLocalBounds().reduced(8, 6);

        if (! problems.isEmpty())
        {
            // Said on the button itself, like a soundboard button's
            // "(file missing)": a scene that silently half-works mid-
            // session is the worst outcome here.
            //
            // A count, not the sentence: at a normal button width the
            // sentence is cut off mid-word. The full reasons are in the
            // tooltip, and in the log when the scene is pressed.
            g.setColour(juce::Colours::white.withAlpha(0.75f));
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawFittedText(problems.size() == 1 ? juce::String("! 1 thing missing")
                                                  : "! " + juce::String(problems.size()) + " things missing",
                              textArea.removeFromBottom(16), juce::Justification::centred, 1, 0.8f);
        }

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
        g.drawFittedText(scene.name, textArea, juce::Justification::centred, 2, 0.8f);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        // Right-click opens the menu and must NOT also fire the scene.
        if (event.mods.isPopupMenu())
        {
            if (onRightClick != nullptr)
                onRightClick();

            return;
        }

        juce::Button::mouseDown(event);
    }

private:
    Scene scene;
    std::function<void()> onRightClick;
    bool active = false;
    juce::StringArray problems;
};

//==============================================================================
ScenesComponent::ScenesComponent(SceneLibrary& libraryToUse, Callbacks callbacksToUse)
    : library(libraryToUse), callbacks(std::move(callbacksToUse))
{
    caption.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    addAndMakeVisible(caption);

    saveCurrentButton.setTooltip("Saves what is playing right now - the playlist, the looping sounds, "
                                  "and optionally the volume - as a scene.");
    saveCurrentButton.onClick = [this] { if (callbacks.saveCurrentAsNew) callbacks.saveCurrentAsNew(); };
    addAndMakeVisible(saveCurrentButton);

    emptyHint.setText("No scenes yet. Get the room sounding right - a playlist, some looping sounds - "
                       "then click \"Save current as scene\". Right-click a scene to change it.",
                       juce::dontSendNotification);
    emptyHint.setJustificationType(juce::Justification::centred);
    addChildComponent(emptyHint);

    viewport.setViewedComponent(&gridPanel, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    lookAndFeelChanged();
    refresh();
}

ScenesComponent::~ScenesComponent() = default;

void ScenesComponent::refresh()
{
    buttons.clear();

    for (int i = 0; i < library.getNumScenes(); ++i)
    {
        auto* scene = library.getScene(i);
        if (scene == nullptr)
            continue;

        auto id = scene->id;
        auto* button = buttons.add(new SceneButton(*scene, [this, id] { showMenuFor(id); }));
        button->onClick = [this, id] { if (callbacks.activate) callbacks.activate(id); };
        button->setActive(id == activeId);

        if (callbacks.problemsFor != nullptr)
            button->setProblems(callbacks.problemsFor(*scene));

        gridPanel.addAndMakeVisible(button);
    }

    emptyHint.setVisible(buttons.isEmpty());
    resized();
}

void ScenesComponent::setActiveScene(const juce::Uuid& id)
{
    activeId = id;

    for (auto* button : buttons)
        button->setActive(button->getSceneId() == id);
}

void ScenesComponent::showMenuFor(const juce::Uuid& id)
{
    auto* scene = library.findById(id);
    if (scene == nullptr)
        return;

    enum { updateItem = 1, editItem, earlierItem, laterItem, deleteItem };

    juce::PopupMenu menu;
    menu.addItem(updateItem, "Update from what's playing now");
    menu.addItem(editItem, "Edit...");
    menu.addSeparator();

    auto index = 0;
    for (int i = 0; i < library.getNumScenes(); ++i)
        if (library.getScene(i)->id == id)
            index = i;

    menu.addItem(earlierItem, "Move earlier", index > 0);
    menu.addItem(laterItem, "Move later", index < library.getNumScenes() - 1);
    menu.addSeparator();
    menu.addItem(deleteItem, "Delete...");

    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
                        [this, safeThis = juce::Component::SafePointer<ScenesComponent>(this), id](int result)
    {
        if (safeThis == nullptr)
            return;

        switch (result)
        {
            case updateItem:  if (callbacks.updateFromCurrent) callbacks.updateFromCurrent(id); break;
            case editItem:    if (callbacks.edit) callbacks.edit(id); break;
            case earlierItem: if (callbacks.move) callbacks.move(id, -1); break;
            case laterItem:   if (callbacks.move) callbacks.move(id, 1); break;
            case deleteItem:  if (callbacks.remove) callbacks.remove(id); break;
            default: break;
        }
    });
}

void ScenesComponent::resized()
{
    auto area = getLocalBounds().reduced(8);

    auto header = area.removeFromTop(kHeaderHeight);
    saveCurrentButton.setBounds(header.removeFromRight(200).reduced(0, 2));
    caption.setBounds(header);
    area.removeFromTop(6);

    emptyHint.setBounds(area.reduced(12));
    viewport.setBounds(area);
    layOutGrid();
}

void ScenesComponent::layOutGrid()
{
    auto area = viewport.getBounds();

    auto usableWidth = juce::jmax(kMinCellWidth, area.getWidth() - viewport.getScrollBarThickness());
    auto columns = juce::jmax(1, (usableWidth + kCellGap) / (kMinCellWidth + kCellGap));
    auto cellWidth = (usableWidth - (columns - 1) * kCellGap) / columns;

    auto rows = (buttons.size() + columns - 1) / columns;
    gridPanel.setSize(usableWidth, juce::jmax(kCellHeight, rows * (kCellHeight + kCellGap)));

    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds((i % columns) * (cellWidth + kCellGap),
                               (i / columns) * (kCellHeight + kCellGap),
                               cellWidth, kCellHeight);
}

// Label colours are COPIES of the palette, so a skin change has to
// re-apply them - see the matching comment in SoundboardGridComponent.
void ScenesComponent::lookAndFeelChanged()
{
    caption.setColour(juce::Label::textColourId, inkwyrd::theme::text);
    emptyHint.setColour(juce::Label::textColourId, inkwyrd::theme::textDim);
}

