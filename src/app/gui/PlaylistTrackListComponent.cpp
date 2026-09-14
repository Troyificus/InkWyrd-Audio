#include "PlaylistTrackListComponent.h"

#include "Dialogs.h"
#include "InkwyrdTheme.h"

namespace
{
    constexpr int kRowHeight = 24;
    constexpr int kCaptionHeight = 24;
    constexpr int kButtonHeight = 26;
}

class PlaylistTrackListComponent::Model : public juce::TableListBoxModel
{
public:
    enum ColumnId { title = 1, artist };

    explicit Model(PlaylistTrackListComponent& ownerToUse) : owner(ownerToUse) {}

    int getNumRows() override { return owner.resolvedTracks.files.size(); }

    void paintRowBackground(juce::Graphics& g, int row, int, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow(row, owner.resolvedTracks.files.size()))
            return;

        if (selected)
            g.fillAll(inkwyrd::theme::accentSoft.withAlpha(0.35f));

        // The now-playing row gets a bar down its left edge as well as
        // brighter text - the mockup's own way of marking it, and it
        // survives being both playing AND selected, where two shades of
        // green would not. On the row background rather than in a cell,
        // so it spans the row whatever the columns are doing.
        if (isPlaying(row))
        {
            g.setColour(inkwyrd::theme::accent);
            g.fillRect(0, 0, 3, height);
        }
    }

    void paintCell(juce::Graphics& g, int row, int columnId, int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow(row, owner.resolvedTracks.files.size()))
            return;

        auto file = owner.resolvedTracks.files[row];
        auto metadata = owner.trackMetadata.get(file);
        auto playing = isPlaying(row);

        juce::String text;

        if (columnId == title)
            text = (playing ? juce::String::fromUTF8("\xe2\x96\xb6 ") : juce::String("   "))
                    + metadata.displayTitle(file);
        else if (columnId == artist)
            text = metadata.artist;

        g.setColour(playing ? inkwyrd::theme::accent : inkwyrd::theme::text);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText(text, juce::Rectangle<int>(6, 0, width - 12, height),
                    juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged(int) override { owner.updateButtons(); }

    void cellClicked(int row, int, const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
            owner.showContextMenuForRow(row);
    }

    void cellDoubleClicked(int row, int, const juce::MouseEvent&) override
    {
        if (juce::isPositiveAndBelow(row, owner.resolvedTracks.files.size()) && owner.onPlayTrack)
            owner.onPlayTrack(owner.shownId, owner.resolvedTracks.files[row]);
    }

    void deleteKeyPressed(int) override { owner.removeSelectedTrack(); }

private:
    bool isPlaying(int row) const
    {
        return owner.resolvedTracks.files[row] == owner.engine.getCurrentTrackFile();
    }

    PlaylistTrackListComponent& owner;
};

PlaylistTrackListComponent::PlaylistTrackListComponent(PlaylistLibrary& libraryToUse,
                                                        TrackMetadataStore& trackMetadataToUse,
                                                        PlaylistEngine& engineToUse,
                                                        std::function<void(const juce::Uuid&)> onPlaylistEditedToUse,
                                                        std::function<void(const juce::Uuid&, const juce::File&)> onPlayTrackToUse,
                                                        std::function<void(const juce::File&)> onPreviewTrackToUse,
                                                        std::function<void(const juce::Array<juce::File>&)> onEditTagsToUse)
    : library(libraryToUse),
      trackMetadata(trackMetadataToUse),
      engine(engineToUse),
      onPlaylistEdited(std::move(onPlaylistEditedToUse)),
      onPlayTrack(std::move(onPlayTrackToUse)),
      onPreviewTrack(std::move(onPreviewTrackToUse)),
      onEditTags(std::move(onEditTagsToUse))
{
    captionLabel.setText("No playlist selected", juce::dontSendNotification);
    captionLabel.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    addAndMakeVisible(captionLabel);

    model = std::make_unique<Model>(*this);
    trackTable.setModel(model.get());
    trackTable.setRowHeight(kRowHeight);

    // visible | resizable only: no sortable flag (see the header comment -
    // the playlist's order IS the play order) and no dragging columns
    // about, which is noise on a two-column table.
    constexpr int kColumnFlags = juce::TableHeaderComponent::visible
                                  | juce::TableHeaderComponent::resizable;

    auto& header = trackTable.getHeader();
    header.addColumn("Title",  Model::title,  240, 120, -1, kColumnFlags);
    header.addColumn("Artist", Model::artist, 140, 70,  -1, kColumnFlags);
    header.setStretchToFitActive(true);

    addAndMakeVisible(trackTable);

    removeButton.onClick = [this] { removeSelectedTrack(); };
    addAndMakeVisible(removeButton);
    updateButtons();

    startTimer(500); // just to keep the playing marker current
}

PlaylistTrackListComponent::~PlaylistTrackListComponent()
{
    // The model outlives the table otherwise.
    trackTable.setModel(nullptr);
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

    trackTable.updateContent();

    // TableListBox only fits its columns to the width in resized(). A
    // playlist long enough to scroll brings in the vertical scrollbar
    // AFTER that, narrowing the space - and the columns, still sized for
    // the wider area, spilled under it with a horizontal scrollbar. So
    // re-fit whenever the rows change.
    trackTable.getHeader().resizeAllColumnsToFit(trackTable.getVisibleContentWidth());
    trackTable.setMinimumContentWidth(trackTable.getHeader().getTotalWidth());

    trackTable.repaint();
    updateButtons();
}

void PlaylistTrackListComponent::updateButtons()
{
    removeButton.setEnabled(trackTable.getSelectedRow() >= 0 && library.findById(shownId) != nullptr);
}

void PlaylistTrackListComponent::showContextMenuForRow(int row)
{
    if (! juce::isPositiveAndBelow(row, resolvedTracks.files.size()))
        return;

    // Right-clicking a row that isn't selected acts on THAT row, like
    // every file manager - otherwise the menu would quietly apply to
    // whatever was selected before.
    if (! trackTable.isRowSelected(row))
        trackTable.selectRow(row);

    auto file = resolvedTracks.files[row];

    enum MenuId { previewItem = 1, editTagsItem, removeItem };

    juce::PopupMenu menu;
    menu.addItem(previewItem, "Preview", onPreviewTrack != nullptr);
    menu.addItem(editTagsItem, "Edit tags...", onEditTags != nullptr);
    menu.addSeparator();
    menu.addItem(removeItem, "Remove from playlist", library.findById(shownId) != nullptr);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                        [this, safeThis = juce::Component::SafePointer<PlaylistTrackListComponent>(this),
                         file](int result)
    {
        if (safeThis == nullptr)
            return;

        if (result == previewItem && onPreviewTrack)
            onPreviewTrack(file);
        else if (result == editTagsItem && onEditTags)
            onEditTags({ file });
        else if (result == removeItem)
            removeSelectedTrack();
    });
}

void PlaylistTrackListComponent::removeSelectedTrack()
{
    auto row = trackTable.getSelectedRow();
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
        trackTable.repaint(); // only the marker moved, row count is unchanged
    }
}

void PlaylistTrackListComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    captionLabel.setBounds(area.removeFromTop(kCaptionHeight));
    area.removeFromTop(6);

    removeButton.setBounds(area.removeFromBottom(kButtonHeight));
    area.removeFromBottom(6);

    trackTable.setBounds(area);
}
