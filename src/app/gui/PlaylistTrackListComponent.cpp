#include "PlaylistTrackListComponent.h"

#include "Dialogs.h"
#include "InkwyrdTheme.h"

namespace
{
    constexpr int kRowHeight = 24;
    constexpr int kCaptionHeight = 24;
    constexpr int kButtonHeight = 26;
}

class PlaylistTrackListComponent::Model : public juce::ListBoxModel
{
public:
    explicit Model(PlaylistTrackListComponent& ownerToUse) : owner(ownerToUse) {}

    int getNumRows() override { return owner.resolvedTracks.files.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow(row, owner.resolvedTracks.files.size()))
            return;

        auto file = owner.resolvedTracks.files[row];
        auto playing = file == owner.engine.getCurrentTrackFile();

        if (selected)
            g.fillAll(inkwyrd::theme::accentSoft.withAlpha(0.35f));

        // The now-playing row gets a bar down its left edge as well as
        // brighter text - the mockup's own way of marking it, and it
        // survives being both playing AND selected, where two shades of
        // green would not.
        if (playing)
        {
            g.setColour(inkwyrd::theme::accent);
            g.fillRect(0, 0, 3, height);
        }

        g.setColour(playing ? inkwyrd::theme::accent : inkwyrd::theme::text);
        auto shownName = owner.trackMetadata.get(file).displayTitle(file);
        g.drawText((playing ? juce::String::fromUTF8("\xe2\x96\xb6 ") : juce::String("   "))
                        + shownName,
                    juce::Rectangle<int>(6, 0, width - 12, height),
                    juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged(int) override { owner.updateButtons(); }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        if (juce::isPositiveAndBelow(row, owner.resolvedTracks.files.size()) && owner.onPlayTrack)
            owner.onPlayTrack(owner.shownId, owner.resolvedTracks.files[row]);
    }

    void deleteKeyPressed(int) override { owner.removeSelectedTrack(); }

private:
    PlaylistTrackListComponent& owner;
};

PlaylistTrackListComponent::PlaylistTrackListComponent(PlaylistLibrary& libraryToUse,
                                                        TrackMetadataStore& trackMetadataToUse,
                                                        PlaylistEngine& engineToUse,
                                                        std::function<void(const juce::Uuid&)> onPlaylistEditedToUse,
                                                        std::function<void(const juce::Uuid&, const juce::File&)> onPlayTrackToUse)
    : library(libraryToUse),
      trackMetadata(trackMetadataToUse),
      engine(engineToUse),
      onPlaylistEdited(std::move(onPlaylistEditedToUse)),
      onPlayTrack(std::move(onPlayTrackToUse))
{
    captionLabel.setText("No playlist selected", juce::dontSendNotification);
    captionLabel.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    addAndMakeVisible(captionLabel);

    model = std::make_unique<Model>(*this);
    trackListBox.setModel(model.get());
    trackListBox.setRowHeight(kRowHeight);
    addAndMakeVisible(trackListBox);

    removeButton.onClick = [this] { removeSelectedTrack(); };
    addAndMakeVisible(removeButton);
    updateButtons();

    startTimer(500); // just to keep the playing marker current
}

PlaylistTrackListComponent::~PlaylistTrackListComponent()
{
    // The model outlives the ListBox otherwise.
    trackListBox.setModel(nullptr);
}

void PlaylistTrackListComponent::setPlaylist(const juce::Uuid& id)
{
    shownId = id;
    refresh();
}

void PlaylistTrackListComponent::refresh()
{
    auto* playlist = library.findById(shownId);

    if (playlist == nullptr)
    {
        resolvedTracks = {};
        captionLabel.setText("No playlist selected", juce::dontSendNotification);
    }
    else
    {
        resolvedTracks = library.resolve(*playlist);
        captionLabel.setText(playlist->name + "  (" + juce::String(resolvedTracks.files.size()) + ")",
                              juce::dontSendNotification);
    }

    trackListBox.updateContent();
    trackListBox.repaint();
    updateButtons();
}

void PlaylistTrackListComponent::updateButtons()
{
    removeButton.setEnabled(trackListBox.getSelectedRow() >= 0 && library.findById(shownId) != nullptr);
}

void PlaylistTrackListComponent::removeSelectedTrack()
{
    auto row = trackListBox.getSelectedRow();
    if (! juce::isPositiveAndBelow(row, resolvedTracks.files.size()))
        return;

    auto* playlist = library.findById(shownId);
    if (playlist == nullptr)
        return;

    auto file = resolvedTracks.files[row];
    auto entryIndex = juce::isPositiveAndBelow(row, resolvedTracks.sourceEntryIndex.size())
                          ? resolvedTracks.sourceEntryIndex[row]
                          : -1;

    if (! juce::isPositiveAndBelow(entryIndex, playlist->entries.size()))
        return;

    // A track that came from a LINKED FOLDER has no row of its own to
    // delete - it exists because the folder does. Removing the entry
    // would silently take every other track from that folder with it, so
    // say what's actually going on rather than doing something drastic.
    if (playlist->entries[entryIndex].kind == PlaylistEntry::Kind::folder)
    {
        inkwyrd::showMessage(this, juce::MessageBoxIconType::InfoIcon,
                              "That track comes from a linked folder",
                              "\"" + file.getFileNameWithoutExtension() + "\" is in \""
                               + playlist->name + "\" because the folder\n"
                               + playlist->entries[entryIndex].path.getFullPathName()
                               + "\nis linked to it, so it can't be removed on its own.\n\n"
                                 "Remove the folder from the playlist to drop all of its tracks, "
                                 "or delete the file itself.");
        return;
    }

    library.removeEntry(shownId, entryIndex);
    refresh();

    if (onPlaylistEdited)
        onPlaylistEdited(shownId);
}

bool PlaylistTrackListComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    if (library.findById(shownId) == nullptr)
        return false; // nothing selected to drop INTO

    for (const auto& path : files)
        if (library.isPlayableFile(juce::File(path)))
            return true;

    return false;
}

void PlaylistTrackListComponent::fileDragEnter(const juce::StringArray&, int, int)
{
    dragActive = true;
    repaint();
}

void PlaylistTrackListComponent::fileDragExit(const juce::StringArray&)
{
    dragActive = false;
    repaint();
}

void PlaylistTrackListComponent::filesDropped(const juce::StringArray& files, int, int)
{
    dragActive = false;
    repaint();

    if (library.findById(shownId) == nullptr)
        return;

    juce::Array<juce::File> playable;
    for (const auto& path : files)
    {
        juce::File file(path);
        if (library.isPlayableFile(file))
            playable.add(file);
    }

    if (playable.isEmpty())
        return;

    library.addFiles(shownId, playable);
    refresh();

    if (onPlaylistEdited)
        onPlaylistEdited(shownId);
}

void PlaylistTrackListComponent::paintOverChildren(juce::Graphics& g)
{
    if (! dragActive)
        return;

    g.setColour(inkwyrd::theme::accent.withAlpha(0.7f));
    g.drawRect(getLocalBounds(), 2);
}

void PlaylistTrackListComponent::timerCallback()
{
    auto current = engine.getCurrentTrackFile();
    if (current != lastSeenPlayingTrack)
    {
        lastSeenPlayingTrack = current;
        trackListBox.repaint(); // only the marker moved, row count is unchanged
    }
}

void PlaylistTrackListComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    captionLabel.setBounds(area.removeFromTop(kCaptionHeight));
    area.removeFromTop(6);

    removeButton.setBounds(area.removeFromBottom(kButtonHeight));
    area.removeFromBottom(6);

    trackListBox.setBounds(area);
}
